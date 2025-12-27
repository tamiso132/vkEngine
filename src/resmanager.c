#include "resmanager.h"
#include "util.h"
#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Constants ---
#define RM_MAX_RESOURCES 1024
#define BINDING_TEXTURES 0
#define BINDING_BUFFERS 1
#define BINDING_IMAGES 2

typedef struct RetiredBuffer {
  RGHandle handle;
  uint64_t frame_retired;
} RetiredBuffer;

// --- Private Prototypes ---
static void _init_bindless(ResourceManager *rm);

static void _rm_update_bindless_sampled(ResourceManager *rm, uint32_t id,
                                        VkImageView view);

static void _rm_update_bindless_buffer(ResourceManager *rm, uint32_t id,
                                       VkBuffer buffer);

static void _rm_update_bindless_storage(ResourceManager *rm, uint32_t id,
                                        VkImageView view);

void rm_init(ResourceManager *rm, GPUDevice *gpu) {
  *rm = (ResourceManager){.gpu = gpu};
  _init_bindless(rm);
}

void rm_destroy(ResourceManager *rm) {
  // 1. Destroy Resources
  for (size_t i = 0; i < v_len(rm->resources); i++) {
    RGResource *r = &rm->resources[i];
    if (!r->is_imported) {
      if (r->type == RES_TYPE_BUFFER) {
        // Assuming you have vmaDestroyBuffer wrapped or accessible via
        // gpu->allocator
        vmaDestroyBuffer(rm->gpu->allocator, r->buf.vk, r->buf.alloc);
      }
      if (r->type == RES_TYPE_IMAGE) {
        vkDestroyImageView(rm->gpu->device, r->img.img.view, NULL);
        vmaDestroyImage(rm->gpu->allocator, r->img.img.vk, r->img.img.alloc);
      }
    }
  }
  v_free(rm->resources);

  // 2. Destroy Bindless Context
  vkDestroySampler(rm->gpu->device, rm->default_sampler, NULL);
  vkDestroyDescriptorSetLayout(rm->gpu->device, rm->bindless_layout, NULL);
  vkDestroyDescriptorPool(rm->gpu->device, rm->descriptor_pool, NULL);
}

RGHandle rm_create_buffer(ResourceManager *rm, const char *name, uint64_t size,
                          VkBufferUsageFlags usage) {
  RGResource res = {.type = RES_TYPE_BUFFER};
  strncpy(res.name, name, 63);

  // Create GPU Resource
  GPUBufferInfo info = {
      .size = size, .usage = usage, .memory_usage = VMA_MEMORY_USAGE_AUTO};
  res.buf = gpu_create_buffer(rm->gpu, &info);

  // Add to Manager & Update Bindless
  uint32_t id = (uint32_t)v_len(rm->resources);
  v_push(rm->resources, res);
  _rm_update_bindless_buffer(rm, id, res.buf.vk);

  return (RGHandle){id};
}

RGHandle rm_create_image(ResourceManager *rm, RGImageInfo info) {
  RGResource res = {.type = RES_TYPE_IMAGE};
  strncpy(res.name, info.name ? info.name : "Unnamed", 63);

  VkImageUsageFlags usage = info.usage;

  // Create GPU Resource
  GPUImageInfo gpu_info = {.extent = {info.width, info.height, 1},
                           .format = info.format,
                           .usage = usage,
                           .debug_name = res.name};

  // Call low-level GPU allocator
  res.img.img = gpu_create_image(rm->gpu, &gpu_info);
  res.img.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

  // Add to Manager
  uint32_t id = (uint32_t)v_len(rm->resources);
  v_push(rm->resources, res);

  // Update Bindless Descriptors based on usage capabilities
  bool is_sampled = (usage & VK_IMAGE_USAGE_SAMPLED_BIT);
  bool is_storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT);

  if (is_sampled) {
    _rm_update_bindless_sampled(rm, id, res.img.img.view);
  }

  if (is_storage) {
    _rm_update_bindless_storage(rm, id, res.img.img.view);
  }

  return (RGHandle){id};
}

RGHandle rm_import_image(ResourceManager *rm, const char *name, VkImage img,
                         VkImageView view, VkImageLayout cur_layout) {
  RGResource res = {.type = RES_TYPE_IMAGE, .is_imported = true};
  strncpy(res.name, name ? name : "Imported", 63);

  res.img.img.vk = img;
  res.img.img.view = view;
  res.img.current_layout =
      cur_layout; // Important: Capture state from previous frame/swapchain

  uint32_t id = (uint32_t)v_len(rm->resources);
  v_push(rm->resources, res);

  return (RGHandle){id};
}

// Call once per frame at the start of the render loop
void rm_process_retirement(ResourceManager *rm) {
  // MAX_FRAMES_IN_FLIGHT (usually 2 or 3)
  uint64_t safe_frame = rm->frame_count - 3;
  for (int i = 0; i < vec_len(&rm->retired_buffers); i++) {
    if (VEC_AT(rm->retired_buffers, i, RetiredBuffer)->frame_retired <
        safe_frame) {

      //
      // Buffer is finally safe from GPU race conditions. Move to free pool.

      vec_push(&rm->free_buffers,
               &VEC_AT(rm->retired_buffers, i, RetiredBuffer)->handle);

      vec_remove_at(&rm->retired_buffers, i--);
    }
  }
}

void rm_retire_buffer(ResourceManager *rm, RGHandle handle) {
  RetiredBuffer rb = {.handle = handle, .frame_retired = rm->frame_count};
  vec_push(&rm->retired_buffers, &rb);
}

RGHandle rm_acquire_reusable_buffer(ResourceManager *rm, uint64_t size,
                                    VkBufferUsageFlags usage) {
  int best_fit = -1;
  uint64_t min_diff = UINT64_MAX;

  // Search free list for a buffer that fits but isn't massively oversized
  for (int i = 0; i < vec_len(&rm->free_buffers); i++) {
    RGHandle *h = VEC_AT(rm->free_buffers, i, RGHandle);
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
    RGHandle h = *VEC_AT(rm->free_buffers, best_fit, RGHandle);
    vec_remove_at(&rm->free_buffers, best_fit);
    return h;
  }

  // No reusable buffer; allocate new memory
  return rm_create_buffer(rm, "VoxelData", size, usage);
}
// --- Implementation: Getters ---

VkBuffer rm_get_buffer(ResourceManager *rm, RGHandle handle) {
  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.id].buf.vk;
}

VkImage rm_get_image(ResourceManager *rm, RGHandle handle) {
  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.id].img.img.vk;
}

VkImageView rm_get_image_view(ResourceManager *rm, RGHandle handle) {
  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.id].img.img.view;
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

static void _rm_update_bindless_sampled(ResourceManager *rm, uint32_t id,
                                        VkImageView view) {
  VkDescriptorImageInfo imgInfo = {
      .sampler = rm->default_sampler,
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .dstBinding = BINDING_TEXTURES,
                                .dstArrayElement = id,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = &imgInfo};
  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}

static void _rm_update_bindless_buffer(ResourceManager *rm, uint32_t id,
                                       VkBuffer buffer) {
  VkDescriptorBufferInfo bufInfo = {
      .buffer = buffer, .offset = 0, .range = VK_WHOLE_SIZE};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .dstBinding = BINDING_BUFFERS,
                                .dstArrayElement = id,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                .pBufferInfo = &bufInfo};
  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}

static void _rm_update_bindless_storage(ResourceManager *rm, uint32_t id,
                                        VkImageView view) {
  VkDescriptorImageInfo imgInfo = {.sampler = VK_NULL_HANDLE,
                                   .imageView = view,
                                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .dstBinding = BINDING_IMAGES,
                                .dstArrayElement = id,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                .pImageInfo = &imgInfo};
  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}
