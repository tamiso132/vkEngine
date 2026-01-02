#include "resmanager.h"
#include "common.h"
#include "gpu/gpu.h"
#include "util.h"
#include "vector.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

// --- Constants ---
#define RM_MAX_RESOURCES 1024
#define INVALID_BINDING_INDEX UINT32_MAX

typedef struct {
  ResType type;
  VmaAllocation alloc;
  u32 frame_retired;
  union {
    struct Buffer {
      VkBuffer handle;
    } buffer;
    struct Image {
      VkImageView view;
      VkImage handle;
    } image;
  };
} RetiredRes;

struct M_Resource {

  u32 frame_count;

  VECTOR_TYPES(RetiredRes)
  Vector retired_res;

  VECTOR_TYPES(RBuffer, RImage)
  Vector resources[RES_TYPE_COUNT];

  // Bindless
  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout bindless_layout;
  VkDescriptorSet bindless_set;

  VkPipelineLayout pip_layout;
  VkSampler default_sampler;
  u32 b_counter[RES_B_COUNT];
};

// --- Private Prototypes ---
static void _destroy(M_Resource *rm);
static void _system_destroy();
static void *_init(M_Resource *rm, M_GPU *gpu);
static bool _system_init(void *config, u32 *mem_req);
static void _create_image_full(M_Resource *rm, RImage *image);
static void _retire_buffer(M_Resource *rm, ResHandle handle);
static void _reset_image_sync(RImage *image);
static void _retire_image(M_Resource *rm, ResHandle handle);
static void _bindless_add(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                          VkDescriptorBufferInfo *bufferInfo);

static void _bindless_update(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                             VkDescriptorBufferInfo *bufferInfo);
static void _init_bindless(M_Resource *rm);

static VkComponentMapping _vk_component_mapping();

SystemFunc rm_system_get_func() {
  return (SystemFunc){
      .on_init = _system_init,
      .on_shutdown = _system_destroy,
  };
}

ResHandle rm_create_buffer(M_Resource *rm, RGBufferInfo *info) {
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  RBuffer buffer = {.sync = {.access = VK_ACCESS_2_NONE, .stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT}};
  strcpy(buffer.name, info->name);

  VkBufferCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = info->capacity, .usage = info->usage};

  VmaAllocationCreateInfo ai = {.requiredFlags = info->mem};

  vmaCreateBuffer(gpu->allocator, &ci, &ai, &buffer.handle, &buffer.alloc, NULL);

  // Add to Manager & Update Bindless
  uint32_t id = (uint32_t)vec_len(&rm->resources[RES_TYPE_BUFFER]);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_BUFFER};

  vec_push(&rm->resources[RES_TYPE_BUFFER], &buffer);

  VkDescriptorBufferInfo descriptorInfo = {};
  descriptorInfo.buffer = buffer.handle;
  descriptorInfo.range = VK_WHOLE_SIZE;

  _bindless_add(rm, resHandle, NULL, &descriptorInfo);

  return resHandle;
}
// I resmanager.c

void rm_resize_image(M_Resource *rm, ResHandle handle, uint32_t width, uint32_t height) {
  _retire_image(rm, handle);

  RImage *image = rm_get_image(rm, handle);
  image->extent.width = width;
  image->extent.height = height;

  _create_image_full(rm, image);
}

void rm_import_existing_image(M_Resource *rm, ResHandle handle, VkImage raw_img, VkImageView view,
                              VkExtent2D new_extent, bool delete_img) {
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  RImage *img = rm_get_image(rm, handle);

  _reset_image_sync(img);
  vkDestroyImageView(gpu->device, img->view, NULL);

  if (delete_img)
    vmaDestroyImage(gpu->allocator, img->handle, img->alloc);

  img->extent = new_extent;
  img->handle = raw_img;
  img->view = view;
}

ResHandle rm_create_image(M_Resource *rm, RGImageInfo info) {
  RImage image = {};
  _reset_image_sync(&image);
  assert(info.name);
  image.name = strdup(info.name);

  VkImageUsageFlags usage = info.usage;

  image.sync.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.extent = (VkExtent2D){.width = info.width, .height = info.height};
  image.usage = info.usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // TODO: fix this
  image.format = info.format;

  _create_image_full(rm, &image);

  bool is_sampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT);
  bool is_storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT);

  if (is_sampled) {
    image.binding = RES_B_SAMPLED_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  } else {
    image.binding = RES_B_STORAGE_IMAGE;
    image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  }

  uint32_t id = vec_len(&rm->resources[RES_TYPE_IMAGE]);
  vec_push(&rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_IMAGE};

  VkDescriptorImageInfo imageInfo = {
      .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED, .imageView = image.view, .sampler = NULL};

  // TODO: fix image sampler
  _bindless_add(rm, resHandle, &imageInfo, NULL);

  return resHandle;
}

ResHandle rm_import_image(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view) {

  RImage image = {
      .name = strdup(info->name),
      .view = view,
      .handle = img,
      .extent = (VkExtent2D){.width = info->width, .height = info->height},
      .usage = info->usage,
  };
  _reset_image_sync(&image);

  uint32_t id = vec_push(&rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_IMAGE};

  return resHandle;
}

void rm_on_new_frame(M_Resource *rm) {
  uint64_t safe_frame = rm->frame_count - 3; // Eller din MAX_FRAMES_IN_FLIGHT
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  for (int i = 0; i < vec_len(&rm->retired_res); i++) {
    RetiredRes *r = VEC_AT(&rm->retired_res, i, RetiredRes);

    if (r->frame_retired < safe_frame) {
      if (r->type == RES_TYPE_BUFFER) {
        vmaDestroyBuffer(gpu->allocator, r->buffer.handle, r->alloc);
      } else if (r->type == RES_TYPE_IMAGE) {
        vkDestroyImageView(gpu->device, r->image.view, NULL);
        vmaDestroyImage(gpu->allocator, r->image.handle, r->alloc);
      }

      // Ta bort från listan (swap-remove är snabbast om ordning ej spelar roll)
      vec_remove_at(&rm->retired_res, i);
      i--;
    }
  }
}

// --- Implementation: Getters ---

RBuffer *rm_get_buffer(M_Resource *rm, ResHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);
  assert(handle.id < vec_len(&rm->resources[handle.res_type]));

  return VEC_AT(&rm->resources[handle.res_type], handle.id, RBuffer);
}

VkPipelineLayout rm_get_pipeline_layout(M_Resource *rm) { return rm->pip_layout; }

RImage *rm_get_image(M_Resource *rm, ResHandle handle) {
  assert(handle.res_type == RES_TYPE_IMAGE);
  assert(handle.id < vec_len(&rm->resources[handle.res_type]));

  return VEC_AT(&rm->resources[handle.res_type], handle.id, RImage);
}

u32 rm_get_buffer_descriptor_index(M_Resource *rm, ResHandle buffer) {
  return rm_get_buffer(rm, buffer)->bindlessIndex;
}

u32 rm_get_image_index(M_Resource *rm, ResHandle image) { return rm_get_image(rm, image)->bindlessIndex; }

VkDescriptorSetLayout rm_get_bindless_layout(M_Resource *rm) { return rm->bindless_layout; }

VkDescriptorSet rm_get_bindless_set(M_Resource *rm) { return rm->bindless_set; }

// --- Private Functions ---

static void _destroy(M_Resource *rm) {
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  Vector *buffers = &rm->resources[RES_TYPE_BUFFER];
  for (size_t i = 0; i < vec_len(buffers); i++) {
    RBuffer *buffer = VEC_AT(buffers, i, RBuffer);
    vmaDestroyBuffer(gpu->allocator, buffer->handle, buffer->alloc);
  }
  Vector *images = &rm->resources[RES_TYPE_IMAGE];

  for (u32 i = 0; i < RES_TYPE_COUNT; i++) {
    vec_free(&rm->resources[i]);
  }

  // 2. Destroy Bindless Context
  vkDestroySampler(gpu->device, rm->default_sampler, NULL);
  vkDestroyDescriptorSetLayout(gpu->device, rm->bindless_layout, NULL);
  vkDestroyDescriptorPool(gpu->device, rm->descriptor_pool, NULL);
}

static void _system_destroy() {
  auto *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  _destroy(rm);
}

static void *_init(M_Resource *rm, M_GPU *gpu) {

  M_GPU *dev = m_system_get(SYSTEM_TYPE_GPU);

  vec_init(&rm->resources[RES_TYPE_IMAGE], sizeof(RImage), NULL);
  vec_init(&rm->resources[RES_TYPE_BUFFER], sizeof(RBuffer), NULL);
  vec_init(&rm->retired_res, sizeof(RetiredRes), NULL);
  _init_bindless(rm);
  return rm;
}

static bool _system_init(void *config, u32 *mem_req) {
  SYSTEM_HELPER_MEM(mem_req, M_Resource);

  auto *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  M_GPU *dev = m_system_get(SYSTEM_TYPE_GPU);

  _init(rm, dev);
  return true;
}

static void _create_image_full(M_Resource *rm, RImage *image) {

  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  _reset_image_sync(image);
  VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                          .imageType = VK_IMAGE_TYPE_2D,
                          .extent = {.width = image->extent.width, .height = image->extent.height, .depth = 1},
                          .mipLevels = 1,
                          .arrayLayers = 1,
                          .format = image->format,
                          .tiling = VK_IMAGE_TILING_OPTIMAL,
                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                          .usage = image->usage,
                          .samples = VK_SAMPLE_COUNT_1_BIT};

  VmaAllocationCreateInfo ai = {.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  vmaCreateImage(gpu->allocator, &ci, &ai, &image->handle, &image->alloc, NULL);
  VkImageViewCreateInfo viewInfo = {
      .image = image->handle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .components = _vk_component_mapping(),
      .format = image->format,
      .subresourceRange =
          (VkImageSubresourceRange){
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
              .levelCount = 1,
          },
  };
  vkCreateImageView(gpu->device, &viewInfo, NULL, &image->view);
}

static void _retire_buffer(M_Resource *rm, ResHandle handle) {

  RBuffer *buffer = rm_get_buffer(rm, handle);
  RetiredRes rb = {.frame_retired = rm->frame_count, .alloc = buffer->alloc, .type = handle.res_type};

  rb.buffer.handle = buffer->handle;

  vec_push(&rm->retired_res, &rb);
}

static void _reset_image_sync(RImage *image) {
  image->sync = (SyncDef){
      .layout = VK_IMAGE_LAYOUT_UNDEFINED, .access = VK_ACCESS_2_NONE, .stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};
}

static void _retire_image(M_Resource *rm, ResHandle handle) {
  RImage *image = rm_get_image(rm, handle);
  RetiredRes rb = {.frame_retired = rm->frame_count, .alloc = image->alloc, .type = handle.res_type};

  rb.image.handle = image->handle;

  vec_push(&rm->retired_res, &rb);
}

static void _bindless_add(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                          VkDescriptorBufferInfo *bufferInfo) {

  void *res = vec_at(&rm->resources[handle.res_type], handle.id);

  if (handle.res_type == RES_TYPE_IMAGE) {
    RImage *image = (RImage *)res;
    image->bindlessIndex = rm->b_counter[image->binding];
    rm->b_counter[image->binding]++;

  } else {
    RBuffer *buffer = (RBuffer *)res;
    buffer->bindlessIndex = rm->b_counter[buffer->binding];
    rm->b_counter[buffer->binding]++;
  }

  _bindless_update(rm, handle, imageInfo, bufferInfo);
}

static void _bindless_update(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                             VkDescriptorBufferInfo *bufferInfo) {
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = imageInfo,
                                .pBufferInfo = bufferInfo};

  void *res = vec_at(&rm->resources[handle.res_type], handle.id);

  if (handle.res_type == RES_TYPE_BUFFER) {
    RBuffer *buffer = (RBuffer *)res;

    write.dstBinding = RES_B_STORAGE_BUFFER;
    write.dstArrayElement = buffer->bindlessIndex;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  }

  else {
    RImage *image = (RImage *)res;

    write.descriptorType = image->type;
    write.dstBinding = image->binding;
    write.dstArrayElement = image->bindlessIndex;
    imageInfo->imageLayout = VK_IMAGE_LAYOUT_GENERAL; // TODO, fix later
  }

  vkUpdateDescriptorSets(gpu->device, 1, &write, 0, NULL);
}

static void _init_bindless(M_Resource *rm) {
  // 1. Create Pool (Must have UPDATE_AFTER_BIND)
  auto *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RM_MAX_RESOURCES},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES}};

  VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                   .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
                                   .maxSets = 1,
                                   .poolSizeCount = 3,
                                   .pPoolSizes = sizes};
  vkCreateDescriptorPool(gpu->device, &pi, NULL, &rm->descriptor_pool);

  // 2. Create Layout
  VkDescriptorSetLayoutBinding bindings[] = {
      // Binding 0: Textures (Sampled)
      {RES_B_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      // Binding 1: Buffers (Storage)
      {RES_B_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      // Binding 2: Images (Storage Write)
      {RES_B_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL}};

  // Allow "partially bound" (holes in array) and "update after bind"
  VkDescriptorBindingFlags f = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorBindingFlags bindFlags[] = {f, f, f};

  VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 3,
      .pBindingFlags = bindFlags};

  VkDescriptorSetLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
                                        .pNext = &flagsInfo,
                                        .bindingCount = 3,
                                        .pBindings = bindings};
  vkCreateDescriptorSetLayout(gpu->device, &li, NULL, &rm->bindless_layout);

  // 3. Allocate Set
  VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                    .descriptorPool = rm->descriptor_pool,
                                    .descriptorSetCount = 1,
                                    .pSetLayouts = &rm->bindless_layout};
  vkAllocateDescriptorSets(gpu->device, &ai, &rm->bindless_set);

  VkPushConstantRange push = {.stageFlags = SHADER_STAGES, .size = 128};

  VkDescriptorSetLayout layout = rm->bindless_layout;
  VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                   .setLayoutCount = 1,
                                   .pSetLayouts = &layout,
                                   .pushConstantRangeCount = 1,
                                   .pPushConstantRanges = &push};

  vk_check(vkCreatePipelineLayout(gpu->device, &pl, NULL, &rm->pip_layout));
  // 4. Default Sampler (Linear)
  VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                            .magFilter = VK_FILTER_LINEAR,
                            .minFilter = VK_FILTER_LINEAR,
                            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                            .maxAnisotropy = 1.0f,
                            .maxLod = VK_LOD_CLAMP_NONE};
  vkCreateSampler(gpu->device, &si, NULL, &rm->default_sampler);
}

static VkComponentMapping _vk_component_mapping() {
  VkComponentMapping d = {.r = VK_COMPONENT_SWIZZLE_R,
                          .g = VK_COMPONENT_SWIZZLE_G,
                          .b = VK_COMPONENT_SWIZZLE_B,
                          .a = VK_COMPONENT_SWIZZLE_A};
  return d;
}
