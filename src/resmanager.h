#pragma once

#include "gpu/gpu.h"
#include "util.h"
#include "vector.h"
#include <stdbool.h>

typedef enum ResourceType {
  RES_TYPE_BUFFER,
  RES_TYPE_IMAGE,
  RES_TYPE_COUNT
} ResourceType;

typedef struct RGHandle {
  u32 id : 31;
  ResourceType res_type : 1;
} RGHandle;

typedef struct RetiredResource {
  ResourceType type;
  union {
    GPUBuffer buf; // Din struct som håller VkBuffer + VmaAllocation
    GPUImage img;  // Din struct som håller VkImage + View + VmaAllocation
  };
  uint64_t frame_retired;
} RetiredResource; // Presets for common use cases
                   //
typedef enum RGImagePreset {
  RG_IMAGETYPE_TEXTURE,    // Sampled, Transfer Dst
  RG_IMAGETYPE_ATTACHMENT, // Color Attachment, Sampled
  RG_IMAGETYPE_STORAGE,    // Storage (RW), Sampled
  RG_IMAGETYPE_DEPTH       // Depth Stencil Attachment
} RGImagePreset;

// Flexible Creation Info
typedef struct RGImageInfo {
  const char *name;
  uint32_t width;
  uint32_t height;
  VkFormat format;
  RGImagePreset preset;    // Use a preset to auto-fill usage
  VkImageUsageFlags usage; // OR explicitly set usage (overrides preset if != 0)
  float scale; // Optional: Scale relative to swapchain (future proofing)
} RGImageInfo;

typedef struct RBuffer {
  char *name;
  u32 bindingIndex;
  VkBuffer vkHandle;
  VmaAllocation alloc;
  u64 size;
  u32 elementSize;
  VkBufferUsageFlags usage;
} RBuffer;

typedef struct RImage {
  char *name;
  ResourceType type;
  bool is_imported;
  u32 descriptor_index;
  VkImageUsageFlags usage;
  VkExtent2D extent;

  VkImage handle;
  VkImageView view;
  VmaAllocation alloc;
} RImage;

typedef struct {
  GPUDevice *gpu;
  u32 frame_count;
  Vector retired_buffers;
  Vector free_buffers;

  Vector *resources[RES_TYPE_COUNT];

  // Bindless
  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout bindless_layout;
  VkDescriptorSet bindless_set;
  VkSampler default_sampler;
} ResourceManager;

// PUBLIC FUNCTIONS
void rm_init(ResourceManager *rm, GPUDevice *gpu);
void rm_destroy(ResourceManager *rm);

RGHandle rm_create_image(ResourceManager *rm, RGImageInfo info);
void rm_process_retirement(ResourceManager *rm);
void rm_retire_buffer(ResourceManager *rm, RGHandle handle);

// Getters
GPUBuffer rm_get_buffer(ResourceManager *rm, RGHandle handle);
VkImage rm_get_image(ResourceManager *rm, RGHandle handle);
VkImageView rm_get_image_view(ResourceManager *rm, RGHandle handle);
VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm);
VkDescriptorSet rm_get_bindless_set(ResourceManager *rm);
