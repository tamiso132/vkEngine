#include "command.h"
#include "common.h"
#include "resource/resmanager.h"
#include "window.h"

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_VULKAN_IMPLEMENTATION
#include "thirdparty/nuklear/nuklear_glfw_vulkan.in.h"

#include "nuklear_backend.h"

static void nk_glfw3_record(VkCommandBuffer command_buffer, uint32_t buffer_index, enum nk_anti_aliasing AA);

NuklearBackend nuklear_backend_init(M_GPU *dev, M_Resource *rm, TWindow *window) {
  VkImageView views[window->swapchain.image_count];
  for (u32 i = 0; i < window->swapchain.image_count; i++) {
    views[i] = rm_get_image(rm, window->swapchain.images[i])->view;
  }

  struct nk_context *ctx =
      nk_glfw3_init(window->raw_window, dev->device, dev->physical_device, dev->graphics_family, views,
                    window->swapchain.image_count, window->swapchain.format, NK_GLFW3_INSTALL_CALLBACKS, 512, 512);

  struct nk_font_atlas *atlas;
  nk_glfw3_font_stash_begin(&atlas);

  struct nk_font *droid = nk_font_atlas_add_from_file(atlas, "assets/ttf/16020_FUTURAM.ttf", 14, 0);

  nk_glfw3_font_stash_end(dev->graphics_queue);

  return (NuklearBackend){.ctx = ctx};
}

void nuklear_backend_new_frame(NuklearBackend *ctx) { nk_glfw3_new_frame(); }

struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self) { return self->ctx; }

void nuklear_backend_render(NuklearBackend *ctx, CmdBuffer cmd, M_GPU *dev, TWindow *window) {

  VkClearValue clear_value = {{{0.0f, 0.0f, 0.0f, 0.0f}}};

  VkRenderPassBeginInfo rp = {0};
  rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp.renderPass = glfw.vulkan.render_pass;
  rp.renderArea.extent.width = (uint32_t)glfw.width;
  rp.renderArea.extent.height = (uint32_t)glfw.height;
  rp.clearValueCount = 1u;
  rp.pClearValues = &clear_value;
  rp.framebuffer = glfw.vulkan.framebuffers[0];

  vkCmdBeginRenderPass(cmd.buffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
  nk_glfw3_record(cmd.buffer, 0, NK_ANTI_ALIASING_OFF);
  vkCmdEndRenderPass(cmd.buffer);
}

static void nk_glfw3_record(VkCommandBuffer command_buffer, uint32_t buffer_index, enum nk_anti_aliasing AA) {
  struct nk_glfw_device *dev = &glfw.vulkan;
  struct nk_buffer vbuf, ebuf;

  struct Mat4f projection = {
      {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f}};

  VkViewport viewport;
  VkDeviceSize doffset = 0;
  VkImageView current_texture = NULL;
  uint32_t index_offset = 0;
  VkRect2D scissor;

  projection.m[0] /= glfw.width;
  projection.m[5] /= glfw.height;

  /* update uniform (host mapped) */
  memcpy(dev->mapped_uniform, &projection, sizeof(projection));

  /* viewport */
  memset(&viewport, 0, sizeof(viewport));
  viewport.width = (float)glfw.width;
  viewport.height = (float)glfw.height;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);

  /* pipeline + base (uniform) set */
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dev->pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dev->pipeline_layout, 0, 1,
                          &dev->uniform_descriptor_set, 0, NULL);

  /* convert from nk command queue into draw list (fills mapped vtx/idx buffers) */
  {
    struct nk_convert_config config;
    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_glfw_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_glfw_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_glfw_vertex, col)},
        {NK_VERTEX_LAYOUT_END}};

    NK_MEMSET(&config, 0, sizeof(config));
    config.vertex_layout = vertex_layout;
    config.vertex_size = sizeof(struct nk_glfw_vertex);
    config.vertex_alignment = NK_ALIGNOF(struct nk_glfw_vertex);
    config.tex_null = dev->tex_null;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = AA;
    config.line_AA = AA;

    nk_buffer_init_fixed(&vbuf, dev->mapped_vertex, (size_t)dev->max_vertex_buffer);
    nk_buffer_init_fixed(&ebuf, dev->mapped_index, (size_t)dev->max_element_buffer);
    nk_convert(&glfw.ctx, &dev->cmds, &vbuf, &ebuf, &config);
  }

  /* bind geometry buffers */
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &dev->vertex_buffer, &doffset);
  vkCmdBindIndexBuffer(command_buffer, dev->index_buffer, 0, VK_INDEX_TYPE_UINT16);

  /* execute each draw command */
  {
    const struct nk_draw_command *cmd;
    nk_draw_foreach(cmd, &glfw.ctx, &dev->cmds) {
      if (!cmd->texture.ptr)
        continue;
      if (!cmd->elem_count)
        continue;

      if (cmd->texture.ptr != current_texture) {
        int found = 0;
        uint32_t i;
        for (i = 0; i < dev->texture_descriptor_sets_len; i++) {
          if (dev->texture_descriptor_sets[i].image_view == cmd->texture.ptr) {
            found = 1;
            break;
          }
        }
        if (!found) {
          /* NOTE: this mirrors your original behavior, but be careful:
             if i == texture_descriptor_sets_len and you haven't allocated
             enough sets, you'll overflow. (Same bug existed before.) */
          update_texture_descriptor_set(dev, &dev->texture_descriptor_sets[i], (VkImageView)cmd->texture.ptr);
          dev->texture_descriptor_sets_len++;
        }

        current_texture = (VkImageView)cmd->texture.ptr;

        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dev->pipeline_layout, 1, 1,
                                &dev->texture_descriptor_sets[i].descriptor_set, 0, NULL);
      }

      scissor.offset.x = (int32_t)(NK_MAX(cmd->clip_rect.x, 0.f));
      scissor.offset.y = (int32_t)(NK_MAX(cmd->clip_rect.y, 0.f));
      scissor.extent.width = (uint32_t)(cmd->clip_rect.w);
      scissor.extent.height = (uint32_t)(cmd->clip_rect.h);

      vkCmdSetScissor(command_buffer, 0, 1, &scissor);
      vkCmdDrawIndexed(command_buffer, cmd->elem_count, 1, index_offset, 0, 0);
      index_offset += cmd->elem_count;
    }

    nk_clear(&glfw.ctx);
  }
}
