#pragma once

#include "resmanager.h"

#include "vector.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vk_mem_alloc.h>

#define RM_MAX_RESOURCES 1024
#define INVALID_BINDING_INDEX UINT32_MAX

// PUBLIC FUNCTIONS

RBuffer *rm_get_buffer(M_Resource *rm, ResHandle handle);
RImage *rm_get_image(M_Resource *rm, ResHandle handle);
ResHandle rm_create_buffer(M_Resource *rm, RGBufferInfo *info);
ResHandle rm_create_image(M_Resource *rm, RGImageInfo info);
ResHandle rm_import_image(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view);
RmStageSlice rm_get_stage_buffer(M_Resource *rm, const void *data, VkDeviceSize size, VkDeviceSize alignment);
SystemFunc rm_system_get_func();
VkDescriptorSet rm_get_bindless_set(M_Resource *rm);
VkDescriptorSetLayout rm_get_bindless_layout(M_Resource *rm);
VkPipelineLayout rm_get_pipeline_layout(M_Resource *rm);
u32 rm_get_buffer_descriptor_index(M_Resource *rm, ResHandle buffer);
u32 rm_get_buffer_index(M_Resource *rm, ResHandle buffer);
u32 rm_get_image_index(M_Resource *rm, ResHandle image);
void rm_destroy(M_Resource *rm);
void rm_import_existing_image(M_Resource *rm, ResHandle handle, VkImage raw_img, VkImageView view, VkExtent2D new_extent, bool delete_img);
void rm_on_new_frame(M_Resource *rm);
void rm_resize_image(M_Resource *rm, ResHandle handle, uint32_t width, uint32_t height);
// END PUBLIC FUNCTIONS
// -------------------- Internal resource structs --------------------



// -------------------- Retirement / GC --------------------

typedef struct {
  ResType type;
  VmaAllocation alloc;
  u32 frame_retired;
  union {
    struct {
      VkBuffer handle;
    } buffer;
    struct {
      VkImageView view;
      VkImage handle;
    } image;
  };
} RetiredRes;

// -------------------- Staging --------------------

typedef struct RmStage {
  VkDeviceSize capacity;

  VkBuffer buffer[1];
  VmaAllocation alloc[1];
  void *mapped[1];
  VkDeviceSize offset[1];

  u32 cur_frame;
  bool is_coherent; // safe default: false (flush)
} RmStage;

// -------------------- RM internal state --------------------

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

  RmStage stage;
};

// -------------------- Internal getters --------------------

static inline RBuffer *rm_get_buffer_internal(M_Resource *rm, ResHandle handle) {
  assert(handle.res_type == RES_TYPE_BUFFER);
  assert(handle.id < vec_len(&rm->resources[handle.res_type]));
  return VEC_AT(&rm->resources[handle.res_type], handle.id, RBuffer);
}

static inline RImage *rm_get_image_internal(M_Resource *rm, ResHandle handle) {
  assert(handle.res_type == RES_TYPE_IMAGE);
  assert(handle.id < vec_len(&rm->resources[handle.res_type]));
  return VEC_AT(&rm->resources[handle.res_type], handle.id, RImage);
}

// -------------------- Internal module APIs --------------------

void rm_bindless_init(M_Resource *rm, M_GPU *gpu);
void rm_bindless_shutdown(M_Resource *rm, M_GPU *gpu);
void rm_bindless_add(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                     VkDescriptorBufferInfo *bufferInfo);
void rm_bindless_update(M_Resource *rm, ResHandle handle, VkDescriptorImageInfo *imageInfo,
                        VkDescriptorBufferInfo *bufferInfo);

VkDeviceSize rm_align_up(VkDeviceSize v, VkDeviceSize a);
void rm_stage_init(M_Resource *rm, M_GPU *gpu, VkDeviceSize capacity_bytes);
void rm_stage_destroy(M_Resource *rm, M_GPU *gpu);
void rm_stage_on_new_frame(M_Resource *rm);
RmStageSlice rm_stage_push(M_Resource *rm, M_GPU *gpu, const void *data, VkDeviceSize size, VkDeviceSize alignment);

void rm_retire_buffer(M_Resource *rm, ResHandle handle);
void rm_retire_image(M_Resource *rm, ResHandle handle);
void rm_retire_on_new_frame(M_Resource *rm, M_GPU *gpu, u32 frames_in_flight);

ResHandle rm_buffer_create(M_Resource *rm, M_GPU *gpu, RGBufferInfo *info);

void rm_reset_image_sync(RImage *image);
void rm_create_image_full(M_Resource *rm, M_GPU *gpu, RImage *image);
ResHandle rm_image_create(M_Resource *rm, M_GPU *gpu, RGImageInfo info);
ResHandle rm_image_import(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view);
void rm_image_destroy_owned(M_Resource *rm, M_GPU *gpu, RImage *img);
