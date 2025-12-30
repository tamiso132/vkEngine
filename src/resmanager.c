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

typedef struct ResourceManager {
  GPUDevice *gpu;
  u32 frame_count;

  VECTOR_TYPES(RetiredRes)
  Vector retired_res;

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
static void _retire_buffer(ResourceManager *rm, ResHandle handle);
static void _retire_image(ResourceManager *rm, ResHandle handle);
static void _bindless_add(ResourceManager *rm, ResHandle handle,
                          VkDescriptorImageInfo *imageInfo,
                          VkDescriptorBufferInfo *bufferInfo);

static void _bindless_update(ResourceManager *rm, ResHandle handle,
                             VkDescriptorImageInfo *imageInfo,
                             VkDescriptorBufferInfo *bufferInfo);
static void _init_bindless(ResourceManager *rm);

static VkComponentMapping _vk_component_mapping();

ResourceManager *rm_init(GPUDevice *gpu) {

  ResourceManager *rm = calloc(sizeof(ResourceManager), 1);
  *rm = (ResourceManager){.gpu = gpu};
  _init_bindless(rm);
  return rm;
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

ResHandle rm_create_buffer(ResourceManager *rm, RGBufferInfo *info) {
  RBuffer buffer = {};
  strcpy(buffer.name, info->name);
  // Create GPU Resource
  GPUBufferInfo bufferInfo = {
      .size = info->capacity, .usage = info->usage, .memory_usage = info->mem};

  VkBufferCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                           .size = info->capacity,
                           .usage = info->usage};

  VmaAllocationCreateInfo ai = {.requiredFlags = info->mem};

  vmaCreateBuffer(rm->gpu->allocator, &ci, &ai, &buffer.handle, &buffer.alloc,
                  NULL);

  // Add to Manager & Update Bindless
  uint32_t id = (uint32_t)vec_len(rm->resources[RES_TYPE_BUFFER]);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_BUFFER};

  vec_push(rm->resources[RES_TYPE_BUFFER], &buffer);

  VkDescriptorBufferInfo descriptorInfo = {};
  descriptorInfo.buffer = buffer.handle;
  descriptorInfo.range = VK_WHOLE_SIZE;

  _bindless_add(rm, resHandle, NULL, &descriptorInfo);

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

  return resHandle;
}

ResHandle rm_import_image(ResourceManager *rm, RGImageInfo *info, VkImage img,
                          VkImageView view, VkImageLayout cur_layout) {
  LOG_ERROR("NOT IMPLEMENTED");
  assert(false);

  RImage image;
  strncpy(image.name, info->name, strlen(info->name));

  image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  image.extent = (VkExtent2D){.width = info->width, .height = info->height};
  image.usage = info->usage;
  image.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

  VkImageViewCreateInfo viewInfo = {
      .image = image.handle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .components = _vk_component_mapping(),
      .format = image.format,
      .subresourceRange = {}, // :TODO fix this, make some util for it
  };

  uint32_t id = vec_len(rm->resources[RES_TYPE_IMAGE]);
  vec_push(rm->resources[RES_TYPE_IMAGE], &image);
  ResHandle resHandle = {.id = id, .res_type = RES_TYPE_IMAGE};

  return resHandle;
}

void rm_on_new_frame(ResourceManager *rm) {
  uint64_t safe_frame = rm->frame_count - 3; // Eller din MAX_FRAMES_IN_FLIGHT

  for (int i = 0; i < vec_len(&rm->retired_res); i++) {
    RetiredRes *r = VEC_AT(&rm->retired_res, i, RetiredRes);

    if (r->frame_retired < safe_frame) {
      if (r->type == RES_TYPE_BUFFER) {
        vmaDestroyBuffer(rm->gpu->allocator, r->buffer.handle, r->alloc);
      } else if (r->type == RES_TYPE_IMAGE) {
        vkDestroyImageView(rm->gpu->device, r->image.view, NULL);
        vmaDestroyImage(rm->gpu->allocator, r->image.handle, r->alloc);
      }

      // Ta bort från listan (swap-remove är snabbast om ordning ej spelar roll)
      vec_remove_at(&rm->retired_res, i);
      i--;
    }
  }
}

void rm_buffer_upload(ResourceManager *rm, VkCommandBuffer cmd,
                      ResHandle handle, void *data, u32 size) {
  RBuffer *buffer = rm_get_buffer(rm, handle);
  // TODO, a check if the buffer is big enough,
  // otherwise might need to return result about needing to resize the buffer
  if (buffer->mem == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
    // TODO, create staging buffer, then copy over all stuff
  } else {
    void *gpu_ptr = {};
    vk_check(vmaMapMemory(rm->gpu->allocator, buffer->alloc, &gpu_ptr));

    memcpy(gpu_ptr, data, size);
  }
};

void rm_buffer_sync(ResourceManager *rm, VkCommandBuffer cmd,
                    BufferBarrierInfo *info) {
  rm_get_buffer(rm, info->buf_handle);

  VkBufferMemoryBarrier2 barrInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .buffer = rm_get_buffer(rm, info->buf_handle)->handle,
      .srcStageMask = info->src_stage,
      .srcAccessMask = info->src_access,
      .dstStageMask = info->dst_stage,
      .dstAccessMask = info->dst_access,
      .size = VK_WHOLE_SIZE,
  };

  VkDependencyInfo dependInfo = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .bufferMemoryBarrierCount = 1,
                                 .pBufferMemoryBarriers = &barrInfo};

  vkCmdPipelineBarrier2(cmd, &dependInfo);
}

void rm_image_sync(ResourceManager *rm, VkCommandBuffer cmd,
                   ImageBarrierInfo *info) {

  VkImageMemoryBarrier2 barrInfo = {
      .image = rm_get_image(rm, info->img_handle)->handle,
      .oldLayout = info->src_layout,
      .srcStageMask = info->src_stage,
      .srcAccessMask = info->src_access,
      .dstStageMask = info->dst_stage,
      .dstAccessMask = info->dst_access,
      .newLayout = info->dst_layout,
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
  };

  VkDependencyInfo dependinfo = {.imageMemoryBarrierCount = 1,
                                 .pImageMemoryBarriers = &barrInfo};

  vkCmdPipelineBarrier2(cmd, &dependinfo);
}

// --- Implementation: Getters ---

GPUDevice *rm_get_gpu(ResourceManager *rm) { return rm->gpu; }

RBuffer *rm_get_buffer(ResourceManager *rm, ResHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);
  assert(handle.id >= vec_len(rm->resources[handle.res_type]));

  return VEC_AT(rm->resources[handle.res_type], handle.id, RBuffer);
}

RImage *rm_get_image(ResourceManager *rm, ResHandle handle) {
  assert(handle.id == RES_TYPE_IMAGE);
  assert(handle.id >= vec_len(rm->resources[handle.res_type]));

  return VEC_AT(rm->resources[handle.res_type], handle.id, RImage);
}

VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm) {
  return rm->bindless_layout;
}

VkDescriptorSet rm_get_bindless_set(ResourceManager *rm) {
  return rm->bindless_set;
}

// --- Private Functions ---

static void _retire_buffer(ResourceManager *rm, ResHandle handle) {

  RBuffer *buffer = rm_get_buffer(rm, handle);
  RetiredRes rb = {.frame_retired = rm->frame_count,
                   .alloc = buffer->alloc,
                   .type = handle.res_type};

  rb.buffer.handle = buffer->handle;

  vec_push(&rm->retired_res, &rb);
}

static void _retire_image(ResourceManager *rm, ResHandle handle) {
  RImage *image = rm_get_image(rm, handle);
  RetiredRes rb = {.frame_retired = rm->frame_count,
                   .alloc = image->alloc,
                   .type = handle.res_type};

  rb.image.handle = image->handle;

  vec_push(&rm->retired_res, &rb);
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
    write.dstBinding = buffer->binding;
    write.dstArrayElement = buffer->bindlessIndex;
  }

  else {
    RImage *image = (RImage *)res;

    write.dstBinding = image->binding;
    write.dstArrayElement = image->bindlessIndex;
  }

  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}

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
      {RES_B_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      // Binding 1: Buffers (Storage)
      {RES_B_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       RM_MAX_RESOURCES, VK_SHADER_STAGE_ALL, NULL},
      // Binding 2: Images (Storage Write)
      {RES_B_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, RM_MAX_RESOURCES,
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

static VkComponentMapping _vk_component_mapping() {
  VkComponentMapping d = {.r = VK_COMPONENT_SWIZZLE_R,
                          .g = VK_COMPONENT_SWIZZLE_G,
                          .b = VK_COMPONENT_SWIZZLE_B,
                          .a = VK_COMPONENT_SWIZZLE_A};
  return d;
}
