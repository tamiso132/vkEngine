#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "gpu/gpu.h"
#include "util.h"
#include "vector.h"
#include <stdbool.h>

// --- Types ---

typedef struct RGHandle {
  uint32_t id;
} RGHandle;

typedef enum RGResourceType { RES_TYPE_BUFFER, RES_TYPE_IMAGE } RGResourceType;

// --- Image Configuration ---

// Presets for common use cases
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

// Internal Resource Representation
typedef struct RGResource {
  char name[64];
  RGResourceType type;
  bool is_imported;

  union {
    GPUBuffer buf;
    struct {
      GPUImage img;
      VkImageLayout current_layout;
    } img;
  };
} RGResource;

typedef struct {
  GPUDevice *gpu;
  u32 frame_count;
  RGResource *resources; // Stretchy Buffer
  Vector retired_buffers;
  Vector free_buffers;
  // Bindless
  VkDescriptorPool descriptor_pool;
  VkDescriptorSetLayout bindless_layout;
  VkDescriptorSet bindless_set;
  VkSampler default_sampler;
} ResourceManager;

// --- API ---

void rm_init(ResourceManager *rm, GPUDevice *gpu);
void rm_destroy(ResourceManager *rm);

// Updated Creator
RGHandle rm_create_image(ResourceManager *rm, RGImageInfo info);

// Legacy/Buffer helpers
RGHandle rm_create_buffer(ResourceManager *rm, const char *name, uint64_t size,
                          VkBufferUsageFlags usage);
RGHandle rm_import_image(ResourceManager *rm, const char *name, VkImage img,
                         VkImageView view, VkImageLayout cur_layout);

// Getters
VkBuffer rm_get_buffer(ResourceManager *rm, RGHandle handle);
VkImage rm_get_image(ResourceManager *rm, RGHandle handle);
VkImageView rm_get_image_view(ResourceManager *rm, RGHandle handle);
VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm);
VkDescriptorSet rm_get_bindless_set(ResourceManager *rm);

#endif
