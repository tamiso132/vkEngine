#pragma once

#include <stdbool.h>

#include "common.h"
#include "gpu/gpu.h"
#include "shaders/shader_base.glsl"
#include "util.h"

// SYNCRONIZATION
typedef enum {
  ACCESS_READ = 1 << 0,
  ACCESS_WRITE = 1 << 1,
} AccessType;

typedef enum {
  STATE_UNDEFINED,
  STATE_SHADER,
  STATE_TRANSFER,
  STATE_COLOR,
  STATE_DEPTH,
  STATE_VERTEX,
  STATE_PRESENT,
  STATE_MAX_ENUM
} ResourceState;

typedef struct {
  VkAccessFlags2 access;
  VkPipelineStageFlags2 stage;
  VkImageLayout layout;
} SyncDef;

typedef enum {
  RES_B_SAMPLED_IMAGE = BINDING_SAMPLED,
  RES_B_STORAGE_IMAGE = BINDING_STORAGE_IMAGE,
  RES_B_STORAGE_BUFFER = BINDING_STORAGE_BUFFER,
  RES_B_COUNT,
} res_b;

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
  float scale;             // Optional: Scale relative to swapchain (future proofing)
} RGImageInfo;

typedef struct {
  const char *name;
  u32 capacity;
  VkBufferUsageFlags2 usage;
  VkMemoryPropertyFlags mem;
} RGBufferInfo;

typedef struct {
  ResHandle img_handle;

  VkImageLayout src_layout;
  VkImageLayout dst_layout;

  VkPipelineStageFlags2 src_stage;
  VkPipelineStageFlags2 dst_stage;

  VkAccessFlags2 src_access;
  VkAccessFlags2 dst_access;

} ImageBarrierInfo;

typedef struct {
  ResHandle buf_handle;

  VkPipelineStageFlags2 src_stage;
  VkPipelineStageFlags2 dst_stage;

  VkAccessFlags2 src_access;
  VkAccessFlags2 dst_access;

} BufferBarrierInfo;

typedef struct {
  char name[50];
  u32 bindlessIndex;
  VkBuffer handle;
  VkMemoryPropertyFlags mem;
  VmaAllocation alloc;
  u64 size;
  u32 capacity;
  VkBufferUsageFlags usage;
  res_b binding;
  VkDescriptorType type;
  SyncDef sync;
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
  SyncDef sync;
} RImage;

// PUBLIC FUNCTIONS

u32 rm_get_buffer_descriptor_index(ResourceManager *rm, ResHandle buffer);
ResourceManager *rm_init(GPUDevice *gpu);
void rm_on_new_frame(ResourceManager *rm);
void rm_destroy(ResourceManager *rm);

ResHandle rm_create_buffer(ResourceManager *rm, RGBufferInfo *info);
void rm_buffer_upload(ResourceManager *rm, VkCommandBuffer cmd, ResHandle handle, void *data, u32 size);
void rm_buffer_sync(ResourceManager *rm, VkCommandBuffer cmd, BufferBarrierInfo *info);

ResHandle rm_create_image(ResourceManager *rm, RGImageInfo info);
void rm_import_existing_image(ResourceManager *rm, ResHandle handle, VkImage raw_img, VkImageView view,
                              VkExtent2D new_extent, bool delete_img);
void rm_resize_image(ResourceManager *rm, ResHandle handle, uint32_t width, uint32_t height);
ResHandle rm_import_image(ResourceManager *rm, RGImageInfo *info, VkImage img, VkImageView view);
void rm_image_sync(ResourceManager *rm, VkCommandBuffer cmd, ImageBarrierInfo *info);

GPUDevice *rm_get_gpu(ResourceManager *rm);
RBuffer *rm_get_buffer(ResourceManager *rm, ResHandle handle);
RImage *rm_get_image(ResourceManager *rm, ResHandle handle);
VkDescriptorSetLayout rm_get_bindless_layout(ResourceManager *rm);
VkDescriptorSet rm_get_bindless_set(ResourceManager *rm);
VkPipelineLayout rm_get_pipeline_layout(ResourceManager *rm);
