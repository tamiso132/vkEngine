#pragma once

#include <stdbool.h>

#include "common.h"
#include "gpu/gpu.h"
#include "shaders/shader_base.glsl"
#include "util.h"

#define SHADER_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)

// -------------------- Synchronization --------------------

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
  RES_B_SAMPLED_IMAGE = BINDING_SAMPLED_IMAGE,
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
  RGImagePreset preset;    // optional
  VkImageUsageFlags usage; // if !=0, overrides preset logic
  float scale;             // optional
} RGImageInfo;

typedef enum BUFFER_QUEUE {
  BUFFER_QUEUE_NONE,
  BUFFER_QUEUE_COUNT = 2,
  BUFFER_QUEUE_GRAPHIC,
  BUFFER_QUEUE_TRANSFER,
  BUFFER_QUEUE_ALL,
} BufferQueue;

typedef struct {
  const char *name;
  u32 capacity;
  VkBufferUsageFlags2 usage;
  VkMemoryPropertyFlags mem;
  BufferQueue queue_type;
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

typedef struct RmStageSlice {
  VkBuffer buffer;
  VkDeviceSize offset;
  VkDeviceSize size;
} RmStageSlice;

typedef struct {
  char name[50];
  i32 bindlessIndex;
  VkBuffer handle;
  VkMemoryPropertyFlags mem;
  VmaAllocation alloc;
  u64 size;
  u32 capacity;
  VkBufferUsageFlags usage; // VkBufferCreateInfo wants VkBufferUsageFlags (not Flags2)
  res_b binding;
  VkDescriptorType type;
  SyncDef sync;
  u32 queue_fam[BUFFER_QUEUE_ALL];
  u32 queue_count;
  VkSharingMode sharing_mode;
} RBuffer;

typedef struct {
  char *name;
  VkDescriptorType type;
  bool is_imported;
  VkImageUsageFlags usage;
  VkExtent2D extent;
  VkFormat format;

  VkImage handle;
  VkImageView view;
  VmaAllocation alloc;

  u32 bindlessIndex;
  res_b binding;
  SyncDef sync;
} RImage;

typedef struct DescriptorInfo {
  ResHandle handle;
  u32 new_index;
} DescriptorInfo;

// -------------------- Public API --------------------

// PUBLIC FUNCTIONS
void rm_bindless_batch_buffer_update(M_Resource *rm, DescriptorInfo *infos, u32 info_count);
ResHandle rm_create_buffer(M_Resource *rm, RGBufferInfo *info);
ResHandle rm_create_image(M_Resource *rm, RGImageInfo info);
void rm_descriptor_update(Vector res);
void rm_destroy(M_Resource *rm);
VkDescriptorSetLayout rm_get_bindless_layout(M_Resource *rm);
VkDescriptorSet rm_get_bindless_set(M_Resource *rm);
RBuffer *rm_get_buffer(M_Resource *rm, ResHandle handle);
u32 rm_get_buffer_descriptor_index(M_Resource *rm, ResHandle buffer);
u32 rm_get_buffer_index(M_Resource *rm, ResHandle buffer);
RImage *rm_get_image(M_Resource *rm, ResHandle handle);
u32 rm_get_image_index(M_Resource *rm, ResHandle image);
VkPipelineLayout rm_get_pipeline_layout(M_Resource *rm);
RmStageSlice rm_get_stage_buffer(M_Resource *rm, const void *data, VkDeviceSize size, VkDeviceSize alignment);
void rm_import_existing_image(M_Resource *rm, ResHandle handle, VkImage raw_img, VkImageView view,
                              VkExtent2D new_extent, bool delete_img);
ResHandle rm_import_image(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view);
void rm_on_new_frame(M_Resource *rm);
void rm_resize_buffer(M_Resource *rm, M_GPU *gpu, u32 new_capacity, ResHandle handle);
void rm_resize_image(M_Resource *rm, ResHandle handle, uint32_t width, uint32_t height);
SystemFunc rm_system_get_func();
// END PUBLIC FUNCTIONS
