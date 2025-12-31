#include "util.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/swapchain.h"
#include "resmanager.h"
#include <vulkan/vulkan_core.h>

// --- Private Prototypes ---
static void _cmd_reset(VkDevice device, CmdBuffer cmd);

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

  VkViewport viewport = {.width = extent.width, .height = extent.height, .minDepth = 0.0f, .maxDepth = 1.0f};
  VkRect2D scissor = {.extent = extent, .offset = {}};

  vkCmdSetScissor(cmd.buffer, 0, 1, &scissor);
  vkCmdSetViewport(cmd.buffer, 0, 1, &viewport);
}

void cmd_bind_pipeline(CmdBuffer cmd, M_Pipeline *pm, BindPipelineInfo *info) {
  GPUPipeline *pipeline = pm_get_pipeline(pm, info->handle);
  VkPipelineLayout layout = rm_get_pipeline_layout(pm_get_rm(pm));

  vkCmdBindPipeline(cmd.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->vk_handle);

  VkPushConstantsInfo push_info = {.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                                   .layout = layout,
                                   .size = info->push_size,
                                   .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                                   .pValues = info->p_push};
  // VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t
  // size, const void* pValues
  vkCmdPushConstants(cmd.buffer, layout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, info->push_size, info->p_push);
}

void cmd_end_rendering(CmdBuffer cmd) { vkCmdEndRendering(cmd.buffer); }

void vk_check(VkResult err) {
  if (err != VK_SUCCESS) {
    LOG_ERROR("VkError: %d", err);

    abort();
  }
}
/**
 * Returns a new heap-allocated substring.
 * start: index to begin at
 * len: number of characters to copy
 */
char *str_sub(const char *s, int start, int len) {
  if (!s || strlen(s) < start)
    return NULL;

  char *sub = malloc(len + 1);
  if (!sub)
    return NULL;

  memcpy(sub, s + start, len);
  sub[len] = '\0';
  return sub;
}

/**
 * Extract directory from path (Non-destructive)
 * Example: "src/main.c" -> returns "src/"
 */
char *str_get_dir(const char *path) {
  char *last_slash = strrchr(path, '/');
#ifdef _WIN32
  char *last_back = strrchr(path, '\\');
  if (last_back > last_slash)
    last_slash = last_back;
#endif

  if (!last_slash)
    return strdup("");

  int len = (int)(last_slash - path) + 1;
  return str_sub(path, 0, len);
}

// --- Private Functions ---

static void _cmd_reset(VkDevice device, CmdBuffer cmd) { vkResetCommandPool(device, cmd.pool, 0); }
