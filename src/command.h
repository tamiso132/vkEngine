#pragma once

#include "common.h"
#include "resource/resmanager.h"
#include "resource/staging_arena.h"

typedef struct CmdBuffer {
  VkCommandPool pool;
  VkCommandBuffer buffer;
  VkDevice device;
} CmdBuffer;

typedef struct RenderingBeginInfo {
  ResHandle *colors;
  u32 colors_count;

  VkAttachmentLoadOp loadOp;
  VkAttachmentStoreOp storeOp;

  vec3 clear_color;
  u32 w;
  u32 h;

  u32 offset_x;
  u32 offset_y;

} RenderingBeginInfo;

typedef struct BindPipelineInfo {
  PipelineHandle handle;
  void *p_push;
  u32 push_size;

} BindPipelineInfo;

// PUBLIC FUNCTIONS
void cmd_begin(VkDevice device, CmdBuffer cmd);
void cmd_begin_rendering(CmdBuffer cmd, M_Resource *rm, RenderingBeginInfo *info);
void cmd_bind_bindless(CmdBuffer cmd, M_Resource *rm, VkExtent2D extent);
void cmd_bind_pipeline(CmdBuffer cmd, M_Pipeline *pm, M_Resource *rm, BindPipelineInfo *info);
void cmd_buffer_copy(CmdBuffer cmd, M_Resource *rm, VmaAllocator allocator, ResHandle dst_handle, StagingSlice slice);
void cmd_buffer_upload(CmdBuffer cmd, M_GPU *dev, M_Resource *rm, ResHandle handle, void *data, u32 size);
void cmd_end(VkDevice device, CmdBuffer cmd);
void cmd_end_rendering(CmdBuffer cmd);
void cmd_image_copy_host(CmdBuffer cmd, M_GPU *dev, M_Resource *rm, ResHandle dst_handle, void *data,
                         VkExtent2D extent);
void cmd_image_copy_to_image(CmdBuffer cmd, M_Resource *rm, ResHandle src_handle, ResHandle dst_handle);
void cmd_image_copy_to_image_offset(CmdBuffer cmd, M_Resource *rm, ResHandle src_handle, ResHandle dst_handle, VkOffset2D offset);

CmdBuffer cmd_init(VkDevice device, u32 queue_fam, const char* name);
void cmd_sync_buffer(CmdBuffer cmd, M_Resource *rm, ResHandle buf_handle, ResourceState dst_state,
                     AccessType dst_access);
void cmd_sync_image(CmdBuffer cmd, M_Resource *rm, ResHandle img_handle, ResourceState dst_state,
                    AccessType dst_access);
// END PUBLIC FUNCTIONS
