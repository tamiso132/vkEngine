#include "resmanager.h"
#include "gpu/gpu.h"
#include "util.h"
#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

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

static void _rm_update_bindless(ResourceManager *rm, u32 index,
                                VkDescriptorImageInfo *imageInfo,
                                VkDescriptorBufferInfo *bufferInfo);

void rm_init(ResourceManager *rm, GPUDevice *gpu) {
  *rm = (ResourceManager){.gpu = gpu};
  _init_bindless(rm);
}

void rm_destroy(ResourceManager *rm) {
  Vector *buffers = rm->resources[RES_TYPE_BUFFER];
  for (size_t i = 0; i < vec_len(buffers); i++) {
    RBuffer *buffer = VEC_AT((*buffers), i, RBuffer);
    vmaDestroyBuffer(rm->gpu->allocator, buffer->vkHandle, buffer->alloc);
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

RGHandle rm_create_buffer(ResourceManager *rm, const char *name, uint64_t size,
                          VkBufferUsageFlags usage) {
  RBuffer buffer = {};
  strncpy(buffer.name, name, strlen(name));
  buffer.name = calloc(strlen(name), 1);
  strcpy(buffer.name, name);
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
// I resmanager.c

void rm_resize_image(ResourceManager *rm, RGHandle handle, uint32_t width,
                     uint32_t height) {
  RGResource *res = &rm->resources[handle.id];

  // Säkerhetskoll: Vi kan inte resize:a importerade bilder (t.ex. Swapchain)
  if (res->is_imported) {
    printf("Error: Cannot resize imported resource %s\n", res->name);
    return;
  }

  // 1. Pensionera den gamla bilden (så den inte raderas medan GPU använder den)
  RetiredResource retired = {
      .type = RES_TYPE_IMAGE,
      .img = res->img.img, // Spara undan den gamla GPU-datan
      .frame_retired = rm->frame_count};
  // Antag att du bytt namn på retired_buffers till retired_resources i din
  // manager
  vec_push(&rm->retired_resources, &retired);

  // 2. Skapa den nya bilden (återanvänd format och usage från den gamla)
  GPUImageInfo new_info = {
      .extent = {width, height, 1},
      .format = res->img.img.format, // Hämta format från den gamla
      .usage = res->img.usage, // Hämta usage flaggorna vi sparade tidigare
      .debug_name = res->name};

  // Ersätt datan i resurs-slotten
  res->img.img = gpu_create_image(rm->gpu, &new_info);
  res->img.current_layout =
      VK_IMAGE_LAYOUT_UNDEFINED; // Ny bild = undefined layout

  // 3. Uppdatera Bindless Descriptor direkt!
  // Eftersom ID:t är detsamma, skriver vi bara över pekaren i descriptor setet.
  _rm_update_descriptor(rm, handle.id);

  printf("Resized image %s (ID: %d) to %dx%d\n", res->name, handle.id, width,
         height);
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

void rm_process_retirement(ResourceManager *rm) {
  uint64_t safe_frame = rm->frame_count - 3; // Eller din MAX_FRAMES_IN_FLIGHT

  for (int i = 0; i < vec_len(&rm->retired_resources); i++) {
    RetiredResource *r = VEC_AT(rm->retired_resources, i, RetiredResource);

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

GPUBuffer rm_get_buffer(ResourceManager *rm, RGHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);

  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.res_type][handle.id];
}

VkImage rm_get_image(ResourceManager *rm, RGHandle handle) {
  assert(handle.id == RES_TYPE_BUFFER);
  if (handle.id >= v_len(rm->resources))
    return VK_NULL_HANDLE;
  return rm->resources[handle.res_type][handle.id];
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

static void _rm_update_bindless(ResourceManager *rm, u32 index,
                                VkDescriptorImageInfo *imageInfo,
                                VkDescriptorBufferInfo *bufferInfo) {

  ;

  VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstSet = rm->bindless_set,
                                .dstBinding = BINDING_TEXTURES,
                                .dstArrayElement = index,
                                .descriptorCount = 1,
                                .descriptorType =
                                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                .pImageInfo = imageInfo,
                                .pBufferInfo = bufferInfo};

  vkUpdateDescriptorSets(rm->gpu->device, 1, &write, 0, NULL);
}
