#pragma once

#include "gpu/gpu.h"
#include "shaders/shader_base.ini"
#include "util.h"
#include "vector.h"
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

typedef enum { RES_TYPE_BUFFER, RES_TYPE_IMAGE, RES_TYPE_COUNT } ResType;

typedef enum {
  RES_B_SAMPLED_IMAGE = BINDING_SAMPLED,
  RES_B_STORAGE_IMAGE = BINDING_STORAGE_IMAGE,
  RES_B_STORAGE_BUFFER = BINDING_STORAGE_BUFFER,
  RES_B_COUNT,
} res_b;

typedef struct {
  u32 id : 31;
  ResType res_type : 1;
} ResHandle;

typedef struct RetiredResource {
  ResType type;
  union {
    GPUBuffer buf; // Din struct som håller VkBuffer + VmaAllocation
    GPUImage img;  // Din struct som håller VkImage + View + VmaAllocation
  };
  uint64_t frame_retired;
} RetiredRes; // Presets for common use cases

typedef enum RGImagePreset {
  RG_IMAGETYPE_TEXTURE,    // Sampled, Transfer Dst
  RG_IMAGETYPE_ATTACHMENT, // Color Attachment, Sampled
  RG_IMAGETYPE_STORAGE,    // Storage (RW), Sampled
  RG_IMAGETYPE_DEPTH       // Depth Stencil Attachment
} RGImagePreset;

// Flexible Creation Info
typedef struct {
  const char *name;
  uint32_t width;
  uint32_t height;
  VkFormat format;
  RGImagePreset preset;    // Use a preset to auto-fill usage
  VkImageUsageFlags usage; // OR explicitly set usage (overrides preset if != 0)
  float scale; // Optional: Scale relative to swapchain (future proofing)
} RGImageInfo;

typedef struct {
  char name[50];
  u32 bindlessIndex;
  VkBuffer handle;
  VmaAllocation alloc;
  u64 size;
  u32 elementSize;
  VkBufferUsageFlags usage;
  res_b binding;
  VkDescriptorType type;
} RBuffer;

typedef struct {
  char *name;
  VkDescriptorType type;
  bool is_imported;
  VkImageUsageFlags usage;
  VkExtent2D extent;
  VkImageLayout layout;
  VkFormat format;

  VkImage handle;
  VkImageView view;
  VmaAllocation alloc;

  u32 bindlessIndex;
  res_b binding;
} RImage;

typedef struct ResourceManager ResourceManager;

// PUBLIC FUNCTIONS
void rm_init(ResourceManager *rm, GPUDevice *gpu);
void rm_destroy(ResourceManager *rm);

ResHandle rm_create_image(ResourceManager *rm, RGImageInfo info);
void rm_process_retirement(ResourceManager *rm);
void rm_retire_buffer(ResourceManager *rm, ResHandle handle);

// Getters
GPUBuffer rm_get_buffer(ResourceManager *rm, ResHandle handle);
VkImage rm_get_image(ResourceManager *rm, ResHandle handle);
VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm);
VkDescriptorSet rm_get_bindless_set(ResourceManager *rm);
