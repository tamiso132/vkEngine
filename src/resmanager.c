#include "resmanager.h"
#include "gpu/gpu.h"
#include "log.h"
#include "shader_base.ini"
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

typedef struct RetiredBuffer {
  ResHandle handle;
  uint64_t frame_retired;
} RetiredBuffer;

typedef struct ResourceManager {
  GPUDevice *gpu;
  u32 frame_count;

  VECTOR_TYPES(RetiredRes)
  Vector retired_buffers;

  VECTOR_TYPES(VkBuffer)
  Vector free_buffers;

  VECTOR_TYPES(RBuffer, RImage)
  Vector *resources[RES_TYPE_COUNT];

  // Bindless
  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout bindless_layout;
  VkDescriptorSet bindless_set;
  VkSampler default_sampler;
  u32 b_counter[RES_B_COUNT];
} ResourceManager;

// --- Private Prototypes ---
static void _init_bindless(ResourceManager *rm);
static void _bindless_add(ResourceManager *rm, ResHandle handle,
                          VkDescriptorImageInfo *imageInfo,
                          VkDescriptorBufferInfo *bufferInfo);

static void _bindless_update(ResourceManager *rm, ResHandle handle,
                             VkDescriptorImageInfo *imageInfo,
                             VkDescriptorBufferInfo *bufferInfo);
static VkComponentMapping _vk_component_mapping();

void rm_init(ResourceManager *rm, GPUDevice *gpu) {
  *rm = (ResourceManager){.gpu = gpu};
  _init_bindless(rm);
}

void rm_destroy(ResourceManager *rm) {
  Vector *buffers = rm->resources[RES_TYPE_BUFFER];
  for (size_t i = 0; i < vec_len(buffers); i++) {
    RBuffer *buffer = VEC_AT(buffers, i, RBuffer);
    vmaDestroyBuffer(rm->gpu->allocator, buffer->handle, buffer->alloc);
  }

  Vector *images = rm->resources[RES_TYPE_IMAGE];

  for (u32 i = 0; i < RES_TYPE_COUNT; i++) {
    vec_free(rm->resources[i]);
  }

  // 2. Destroy Bindless Context
  vkDestroySampler(rm->gpu->device, rm->default_sampler, NULL);
  vkDestroyDescriptorSetLayout(rm->gpu->device, rm->bindless_layout, NULL);
  vkDestroyDescriptorPool(rm->gpu->device, rm->descriptor_pool, NULL);
}

ResHandle rm_create_buffer(ResourceManager *rm, const char *name, uint64_t size,
                           VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags memory) {
  RBuffer buffer = {};
  strcpy(buffer.name, name);
  // Create GPU Resource
  GPUBufferInfo info = {
      .size = size, .usage = usage, .memory_usage = VMA_MEMORY_USAGE_AUTO};

  GPUBuffer b = {0};
  VkBufferCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = size,
                           .usage = usage};
  VmaAllocationCreateInfo ai = {.requiredFlags = memory};

  vmaCreateBuffer(rm->gpu->allocator, &ci, &ai, &buffer.handle, &buffer.alloc,
                  NULL);

  // Add to Manager & Update Bindless
  uint32_t id = (uint32_t)vec_len(rm->resources[RES_TYPE_BUFFER]);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_BUFFER};

  vec_push(rm->resources[RES_TYPE_BUFFER], &buffer);

  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = buffer.handle;
  bufferInfo.range = VK_WHOLE_SIZE;

  _bindless_add(rm, resHandle, NULL, &bufferInfo);

  return resHandle;
}
// I resmanager.c

void rm_resize_image(ResourceManager *rm, ResHandle handle, uint32_t width,
                     uint32_t height) {}

ResHandle rm_create_image(ResourceManager *rm, RGImageInfo info) {
  RImage image;
  assert(info.name);
  strncpy(image.name, info.name, strlen(info.name));

  VkImageUsageFlags usage = info.usage;

  VkImageCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .extent = {.width = info.width, .height = info.height, .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .format = info.format,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .usage = info.usage,
      .samples = VK_SAMPLE_COUNT_1_BIT};

  VmaAllocationCreateInfo ai = {.requiredFlags =
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  vmaCreateImage(rm->gpu->allocator, &ci, &ai, &image.handle, &image.alloc,
                 NULL);

  image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.extent = (VkExtent2D){.width = info.width, .height = info.height};
  image.usage = info.usage;
  image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // TODO: fix this
  VkImageViewCreateInfo viewInfo = {
      .image = image.handle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .components = _vk_component_mapping(),
      .format = image.format,
      .subresourceRange = {}, // :TODO fix this, make some util for it
  };

  bool is_sampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT);
  bool is_storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT);

  if (is_sampled) {
    image.binding = RES_B_SAMPLED_IMAGE;
  } else {
    image.binding = RES_B_STORAGE_IMAGE;
  }

  uint32_t id = vec_len(rm->resources[RES_TYPE_IMAGE]);
  vec_push(rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_IMAGE};

  VkDescriptorImageInfo imageInfo = {.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                     .imageView = image.view,
                                     .sampler = NULL};

  // TODO: fix image sampler
  _bindless_add(rm, resHandle, &imageInfo, NULL);

  return (ResHandle){id};
}

ResHandle rm_import_image(ResourceManager *rm, const char *name, VkImage img,
                          VkImageView view, VkImageLayout cur_layout) {
  LOG_ERROR("NOT IMPLEMENTED");
  assert(false);
}

void rm_process_retirement(ResourceManager *rm) {
  uint64_t safe_frame = rm->frame_count - 3; // Eller din MAX_FRAMES_IN_FLIGHT

  for (int i = 0; i < vec_len(&rm->retired_resources); i++) {
    RetiredRes *r = VEC_AT(rm->retired_resources, i, RetiredRes);

    if (r->frame_retired < safe_frame) {
      // Nu är det säkert att förstöra objektet på riktigt
      if (r->type == RES_TYPE_BUFFER) {
        vmaDestroyBuffer(rm->gpu->allocator, r->buf.vk, r->buf.alloc);
      } else if (r->type == RES_TYPE_IMAGE) {
        vkDestroyImageView(rm->gpu->device, r->img.view, NULL);
        vmaDestroyImage(rm->gpu->allocator, r->img.vk, r->img.alloc);
      }

      // Ta bort från listan (swap-remove är snabbast om ordning ej spelar roll)
      vec_remove_at(&rm->retired_resources, i--);
    }
  }
}
void rm_retire_buffer(ResourceManager *rm, ResHandle handle) {
  RetiredBuffer rb = {.handle = handle, .frame_retired = rm->frame_count};
  vec_push(&rm->retired_buffers, &rb);
}

ResHandle rm_acquire_reusable_buffer(ResourceManager *rm, uint64_t size,
                                     VkBufferUsageFlags usage) {
  int best_fit = -1;
  uint64_t min_diff = UINT64_MAX;

  // Search free list for a buffer that fits but isn't massively oversized
  for (int i = 0; i < vec_len(&rm->free_buffers); i++) {
    ResHandle *h = VEC_AT(rm->free_buffers, i, ResHandle);
    uint64_t b_size = rm->resources[h->id].buf.size;

    if (b_size >= size) {
      uint64_t diff = b_size - size;
      if (diff < min_diff) {
        min_diff = diff;
        best_fit = i;
      }
    }
  }

  if (best_fit != -1) {
    ResHandle h = *VEC_AT(rm->free_buffers, best_fit, ResHandle);
    vec_remove_at(&rm->free_buffers, best_fit);
    return h;
  }

  // No reusable buffer; allocate new memory
  return rm_create_buffer(rm, "VoxelData", size, usage);
}
// --- Implementation: Getters ---

GPUBuffer rm_get_buffer(ResourceManager *rm, ResHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);

  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.res_type][handle.id];
}

RImage *rm_get_image(ResourceManager *rm, ResHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);
  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return &rm->resources[handle.res_type][handle.id];
}

VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm) {
  return rm->bindless_layout;
}

VkDescriptorSet rm_get_bindless_set(ResourceManager *rm) {
  return rm->bindless_set;
}

// --- Private Functions ---
static void _init_bindless(ResourceManager *rm) {
  // 1. Create Pool (Must have UPDATE_AFTER_BIND)
  VkDescriptorPoolSize sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RM_MAX_RESOURCES},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES}};

  VkDescriptorPoolCreateInfo pi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = 3,
      .pPoolSizes = sizes};
  vkCreateDescriptorPool(rm->gpu->device, &pi, NULL, &rm->descriptor_pool);

  // 2. Create Layout
  VkDescriptorSetLayoutBinding bindings[] = {
      // Binding 0: Textures (Sampled)
      {BINDING_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      // Binding 1: Buffers (Storage)
      {BINDING_BUFFERS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RM_MAX_RESOURCES,
       VK_SHADER_STAGE_ALL, NULL},
      // Binding 2: Images (Storage Write)
      {BINDING_IMAGES, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES,
       VK_SHADER_STAGE_ALL, NULL}};

  // Allow "partially bound" (holes in array) and "update after bind"
  VkDescriptorBindingFlags f = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                               VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  VkDescriptorBindingFlags bindFlags[] = {f, f, f};

  VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo = {
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = 3,
      .pBindingFlags = bindFlags};

  VkDescriptorSetLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .pNext = &flagsInfo,
      .bindingCount = 3,
      .pBindings = bindings};
  vkCreateDescriptorSetLayout(rm->gpu->device, &li, NULL, &rm->bindless_layout);

  // 3. Allocate Set
  VkDescriptorSetAllocateInfo ai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = rm->descriptor_pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &rm->bindless_layout};
  vkAllocateDescriptorSets(rm->gpu->device, &ai, &rm->bindless_set);

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
  vkCreateSampler(rm->gpu->device, &si, NULL, &rm->default_sampler);
}

static void _bindless_add(ResourceManager *rm, ResHandle handle,
                          VkDescriptorImageInfo *imageInfo,
                          VkDescriptorBufferInfo *bufferInfo) {

  void *res = vec_at(rm->resources[handle.res_type], handle.id);

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

static void _bindless_update(ResourceManager *rm, ResHandle handle,
                             VkDescriptorImageInfo *imageInfo,
                             VkDescriptorBufferInfo *bufferInfo) {
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = imageInfo,
                                .pBufferInfo = bufferInfo};

  void *res = vec_at(rm->resources[handle.res_type], handle.id);

  if (handle.res_type == RES_TYPE_BUFFER) {
    RBuffer *buffer = (RBuffer *)res;
    write.dstBinding = buffer->bindingIndex;
    write.dstArrayElement = buffer->bindlessIndex;
  }

  else {
    RImage *image = (RImage *)res;

    write.dstBinding = image->binding;
    write.dstArrayElement = image->bindlessIndex;
  }

  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}

static VkComponentMapping _vk_component_mapping() {
  VkComponentMapping d = {.r = VK_COMPONENT_SWIZZLE_R,
                          .g = VK_COMPONENT_SWIZZLE_G,
                          .b = VK_COMPONENT_SWIZZLE_B,
                          .a = VK_COMPONENT_SWIZZLE_A};
  return d;
}
