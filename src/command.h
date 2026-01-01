#pragma once
#include "resmanager.h"

typedef struct CmdBuffer {
  VkCommandPool pool;
  VkCommandBuffer buffer;
} CmdBuffer;

typedef struct RenderingBeginInfo {
  ResHandle *colors;
  u32 colors_count;

  VkAttachmentLoadOp loadOp;
  VkAttachmentStoreOp storeOp;

  vec3 clear_color;
  u32 w;
  u32 h;

} RenderingBeginInfo;

typedef struct BindPipelineInfo {
  PipelineHandle handle;
  void *p_push;
  u32 push_size;

} BindPipelineInfo;

// PUBLIC FUNCTIONS

 void cmd_image_copy_to_image(CmdBuffer cmd, ResourceManager *rm, ResHandle src_handle, ResHandle dst_handle);

 CmdBuffer cmd_init(VkDevice device, u32 queue_fam);

 void cmd_begin(VkDevice device, CmdBuffer cmd);
 void cmd_end(VkDevice device, CmdBuffer cmd);

 void cmd_begin_rendering(CmdBuffer cmd, ResourceManager *rm, RenderingBeginInfo *info);
 void cmd_end_rendering(CmdBuffer cmd);

 void cmd_bind_bindless(CmdBuffer cmd, ResourceManager *rm, VkExtent2D extent);
 void cmd_bind_pipeline(CmdBuffer cmd, M_Pipeline *pm, BindPipelineInfo *info);

 void cmd_sync_image(CmdBuffer cmd, ResourceManager *rm, ResHandle img_handle, ResourceState dst_state, AccessType dst_access);

 void cmd_buffer_upload(CmdBuffer cmd, ResourceManager *rm, ResHandle handle, void *data, u32 size);
 void cmd_sync_buffer(VkCommandBuffer cmd, ResourceManager *rm, ResHandle buf_handle, ResourceState dst_state, AccessType dst_access);
