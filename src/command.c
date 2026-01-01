#include "command.h"
#include "common.h"
#include "gpu/gpu.h"
#include "gpu/pipeline.h"
#include "resmanager.h"
#include "vector.h"

typedef struct {
  VkPipelineStageFlags2 stage;
  VkAccessFlags2 read_access;
  VkAccessFlags2 write_access;
  VkImageLayout read_layout;
  VkImageLayout write_layout;
} StateProperties;

// --- Private Prototypes ---
static SyncDef _resolve_sync(ResourceState state, AccessType access);
static void _cmd_reset(VkDevice device, CmdBuffer cmd);

// The Master Lookup Table
static const StateProperties STATE_TABLE[] = {[STATE_SHADER] = {.stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                                .read_access = VK_ACCESS_2_SHADER_READ_BIT,
                                                                .write_access = VK_ACCESS_2_SHADER_WRITE_BIT,
                                                                .read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                                .write_layout = VK_IMAGE_LAYOUT_GENERAL},

                                              [STATE_TRANSFER] = {.stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                                                  .read_access = VK_ACCESS_2_TRANSFER_READ_BIT,
                                                                  .write_access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                                                  .read_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                                  .write_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},

                                              [STATE_COLOR] = {.stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                                               .read_access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                                                               .write_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                                               .read_layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                                                               .write_layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL},

                                              [STATE_PRESENT] = {.stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                                                 .read_access = 0,
                                                                 .write_access = 0,
                                                                 .read_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                                                 .write_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR}

};

CmdBuffer cmd_init(VkDevice device, u32 queue_fam) {
  CmdBuffer cmd = {};
  VkCommandPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                  .queueFamilyIndex = queue_fam};

  vk_check(vkCreateCommandPool(device, &info, NULL, &cmd.pool));
  VkCommandBufferAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                           .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                           .commandPool = cmd.pool,
                                           .commandBufferCount = 1};

  vk_check(vkAllocateCommandBuffers(device, &allocInfo, &cmd.buffer));
  return cmd;
}

// Helper to start a one-time command buffer
void cmd_begin(VkDevice device, CmdBuffer cmd) {

  _cmd_reset(device, cmd);
  VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

  vk_check(vkBeginCommandBuffer(cmd.buffer, &beginInfo));
}
// Helper to end and flush the command buffer immediately
void cmd_end(VkDevice device, CmdBuffer cmd) { vk_check(vkEndCommandBuffer(cmd.buffer)); }

void cmd_begin_rendering(CmdBuffer cmd, ResourceManager *rm, RenderingBeginInfo *info) {

  VkRenderingAttachmentInfo colors_view[info->colors_count];
  for (u32 i = 0; i < info->colors_count; i++) {
    auto image = rm_get_image(rm, info->colors[i]);
    colors_view[i] = (VkRenderingAttachmentInfo){
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = image->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = info->loadOp,
        .storeOp = info->storeOp,
        .clearValue = {{{info->clear_color[0], info->clear_color[1], info->clear_color[2], 1.0f}}}};
  }

  // 3. The Main Rendering Info
  VkRenderingInfo render_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .layerCount = 1,
      .colorAttachmentCount = info->colors_count,
      .pColorAttachments = colors_view,
      .pStencilAttachment = VK_NULL_HANDLE,

      .renderArea = {.offset = {0, 0}, .extent = {.width = info->w, .height = info->h}},
  };

  // 4. Begin!
  vkCmdBeginRendering(cmd.buffer, &render_info);
}

void cmd_bind_bindless(CmdBuffer cmd, ResourceManager *rm, VkExtent2D extent) {
  VkDescriptorSet set = rm_get_bindless_set(rm);
  VkPipelineLayout pip_layout = rm_get_pipeline_layout(rm);

  vkCmdBindDescriptorSets(cmd.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pip_layout, 0, 1, &set, 0, NULL);
  vkCmdBindDescriptorSets(cmd.buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pip_layout, 0, 1, &set, 0, NULL);

  VkViewport viewport = {.width = extent.width, .height = extent.height, .minDepth = 0.0f, .maxDepth = 1.0f};
  VkRect2D scissor = {.extent = extent, .offset = {}};

  vkCmdSetScissor(cmd.buffer, 0, 1, &scissor);
  vkCmdSetViewport(cmd.buffer, 0, 1, &viewport);
}

void cmd_bind_pipeline(CmdBuffer cmd, M_Pipeline *pm, BindPipelineInfo *info) {
  GPUPipeline *pipeline = pm_get_pipeline(pm, info->handle);
  VkPipelineLayout layout = rm_get_pipeline_layout(pm_get_rm(pm));

  VkPipelineBindPoint point =
      pipeline->type == PIPELINE_TYPE_COMPUTE ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

  vkCmdBindPipeline(cmd.buffer, point, pipeline->vk_handle);

  VkPushConstantsInfo push_info = {.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                   .layout = layout,
                                   .size = info->push_size,
                                   .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                   .pValues = info->p_push};
  // VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t
  // size, const void* pValues

  vkCmdPushConstants(cmd.buffer, layout, SHADER_STAGES, 0, info->push_size, info->p_push);
}

void cmd_end_rendering(CmdBuffer cmd) { vkCmdEndRendering(cmd.buffer); }

void cmd_image_copy_to_image(CmdBuffer cmd, ResourceManager *rm, ResHandle src_handle, ResHandle dst_handle) {
  RImage *src_img = rm_get_image(rm, src_handle);
  RImage *dst_img = rm_get_image(rm, dst_handle);

  if (src_img->sync.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    cmd_sync_image(cmd, rm, src_handle, STATE_TRANSFER, ACCESS_READ);
  }

  if (dst_img->sync.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    cmd_sync_image(cmd, rm, dst_handle, STATE_TRANSFER, ACCESS_WRITE);
  }
  VkOffset3D bound[] = {{}, (VkOffset3D){.x = src_img->extent.width, .y = src_img->extent.height, .z = 1}};

  VkImageBlit2 blitInfo = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
      .srcSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
          },
      .srcOffsets = {bound[0], bound[1]},
      .dstSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .layerCount = 1,
          },
      .dstOffsets = {bound[0], bound[1]},
  };

  VkBlitImageInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
      .dstImageLayout = dst_img->sync.layout,
      .dstImage = dst_img->handle,
      .srcImageLayout = src_img->sync.layout,
      .srcImage = src_img->handle,
      .pRegions = &blitInfo,
      .regionCount = 1,
  };

  vkCmdBlitImage2(cmd.buffer, &info);
}

void cmd_sync_image(CmdBuffer cmd, ResourceManager *rm, ResHandle img_handle, ResourceState dst_state,
                    AccessType dst_access) {

  RImage *img = rm_get_image(rm, img_handle);
  SyncDef src_sync = img->sync;
  SyncDef dst_sync = _resolve_sync(dst_state, dst_access);

  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcAccessMask = src_sync.access,
      .srcStageMask = src_sync.stage,
      .oldLayout = src_sync.layout,

      .dstAccessMask = dst_sync.access,
      .dstStageMask = dst_sync.stage,
      .newLayout = dst_sync.layout,

      .image = img->handle,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1, .levelCount = 1}};

  VkDependencyInfo info = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, &barrier};

  img->sync = dst_sync;
  vkCmdPipelineBarrier2(cmd.buffer, &info);
}

void cmd_sync_buffer(VkCommandBuffer cmd, ResourceManager *rm, ResHandle buf_handle, ResourceState dst_state,
                     AccessType dst_access) {

  RBuffer *buffer = rm_get_buffer(rm, buf_handle);

  SyncDef src = buffer->sync;
  SyncDef dst = _resolve_sync(dst_state, dst_access);

  VkBufferMemoryBarrier2 barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                    .srcStageMask = src.stage,
                                    .srcAccessMask = src.access,
                                    .dstStageMask = dst.stage,
                                    .dstAccessMask = dst.access,
                                    .buffer = buffer->handle,
                                    .offset = 0,
                                    .size = VK_WHOLE_SIZE};

  VkDependencyInfo dep = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier};

  buffer->sync = dst;
  vkCmdPipelineBarrier2(cmd, &dep);
}

void cmd_buffer_upload(CmdBuffer cmd, ResourceManager *rm, ResHandle handle, void *data, u32 size) {
  RBuffer *buffer = rm_get_buffer(rm, handle);

  // TODO, a check if the buffer is big enough,
  // otherwise might need to return result about needing to resize the buffer
  if (buffer->mem == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
    // TODO, create staging buffer, then copy over all stuff
    // TODO, ask resource manager for a staging buffer of X size
    // TODO, transfer buffer stage to src
  } else {
    void *gpu_ptr = {};
    VmaAllocator allocator = rm_get_gpu(rm)->allocator;
    vk_check(vmaMapMemory(allocator, buffer->alloc, &gpu_ptr));

    memcpy(gpu_ptr, data, size);

    vmaUnmapMemory(allocator, buffer->alloc);
  }
};

// --- Private Functions ---

static SyncDef _resolve_sync(ResourceState state, AccessType access) {
  const StateProperties *props = &STATE_TABLE[state];
  SyncDef res = {.stage = props->stage};

  if (access & ACCESS_WRITE) {
    res.access = props->write_access;
    res.layout = props->write_layout;
    // If it's Read+Write (Atomics/Storage), combine access flags
    if (access & ACCESS_READ)
      res.access |= props->read_access;
  } else {
    res.access = props->read_access;
    res.layout = props->read_layout;
  }

  return res;
}

static void _cmd_reset(VkDevice device, CmdBuffer cmd) { vkResetCommandPool(device, cmd.pool, 0); }
