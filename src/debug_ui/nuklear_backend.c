#include "cglm/types.h"
#include "command.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "panel.h"
#include "resource/resmanager.h"
#include "util.h"
#include "window.h"

#include "nuklear_backend.h"
#include <assert.h>
#include <vulkan/vulkan_core.h>

#include "shaders/ui_shared.glsl"

#include "gpu/pipeline_hotreload.h"
#include "input.h"

#define NK_ASSERT assert
#define NK_MEMSET memset

#define NK_IMPLEMENTATION
#include  "nuklear_config.h"




typedef struct NuklearBackend {
  struct nk_context ctx;
  PipelineHandle pipeline;
  ResHandle res_font;
  ResHandle res_vertex;
  ResHandle res_index;
  struct nk_font_atlas atlas;

  struct nk_buffer vbuf;
  struct nk_buffer ebuf;
  struct nk_buffer nk_cmds;

  struct nk_allocator allocator;

  void* mapped_vtx;
  void* mapped_idx;
} NuklearBackend;


static const struct nk_draw_vertex_layout_element NK_VERTEX_LAYOUT[] = {
    {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(GPUNuklearVertex, pos)},
    {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(GPUNuklearVertex, uv)},
    {NK_VERTEX_COLOR,    NK_FORMAT_R8G8B8A8, NK_OFFSETOF(GPUNuklearVertex, color)},
    {NK_VERTEX_LAYOUT_END}
};


void _ensure_ui_buffers_size(NuklearBackend* b, M_Resource* rm, M_GPU* dev);

static int _convert_try(NuklearBackend* b, M_Resource* rm,
                       void* mapped_vtx, size_t vtx_cap,
                       void* mapped_idx, size_t idx_cap);

// --- Private Prototypes ---
static void nuklear_backend_tick(struct nk_context *nk, Input *in);

// NK_API void nk_glfw3_font_stash_end(VkQueue graphics_queue) {
//     struct nk_glfw_device *dev = &glfw.vulkan;

//     const void *image;
//     int w, h;
//     image = nk_font_atlas_bake(&glfw.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
//     nk_glfw3_device_upload_atlas(graphics_queue, image, w, h);
//     nk_font_atlas_end(&glfw.atlas, nk_handle_ptr(dev->font_image_view),
//                       &dev->tex_null);
//     if (glfw.atlas.default_font) {
//         nk_style_set_font(&glfw.ctx, &glfw.atlas.default_font->handle);
//     }
// }

NuklearBackend* nuklear_backend_init(
    M_GPU *dev, M_Resource *rm, CmdBuffer main_cmd,
    TWindow *window, M_HotReload* hotreloader)
{
    NuklearBackend *backend =    calloc(1, sizeof(NuklearBackend));
    // 1) allocator + ctx
    backend->allocator.alloc = nk_malloc;
    backend->allocator.free  = nk_mfree;
    backend->allocator.userdata = nk_handle_id(0);

    nk_init(&backend->ctx, &backend->allocator, NULL);

    // Nuklear uses an internal "command buffer" to store draw commands before convert
    nk_buffer_init(&backend->nk_cmds, &backend->allocator, 4 * 1024);

    // 2) font atlas -> bake -> upload to VkImage
    nk_font_atlas_init_default(&backend->atlas);
    nk_font_atlas_begin(&backend->atlas);

    struct nk_font *font = nk_font_atlas_add_from_file(
        &backend->atlas, "assets/ttf/16020_FUTURAM.ttf", 14.0f, NULL);

    int w = 0, h = 0;
    const void *rgba = nk_font_atlas_bake(&backend->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    // IMPORTANT: baked data is RGBA8, so use R8G8B8A8
    RGImageInfo font_info = {
        .name   = "NK-Font-Texture",
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage  = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .width  = (u32)w,
        .height = (u32)h
    };

    backend->res_font = rm_create_image(rm, font_info);

    VkExtent2D font_extent = {.width = (u32)w, .height = (u32)h};
    cmd_image_copy_host(main_cmd, dev, rm, backend->res_font, (void*)rgba, font_extent);

    // This is what your renderer will treat as "texture id"
    VkImageView font_view = rm_get_image(rm, backend->res_font)->view;

    // Provide a null texture too (recommended)
    nk_font_atlas_end(&backend->atlas, nk_handle_ptr((void*)font_view), NULL);

    if (font) nk_style_set_font(&backend->ctx, &font->handle);

    // 3) GPU buffers (FIX: correct Vulkan usage bits!)
    RGBufferInfo vert_info = {
        .capacity   = sizeof(GPUNuklearVertex) * 8192, // pick a real budget
        .mem        = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .name       = "VertexBuffer-Nuklear",
        .queue_type = BUFFER_QUEUE_GRAPHIC,
        .usage      = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    };

    RGBufferInfo index_info = {
        .capacity   = sizeof(u16) * 16384,
        .mem        = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        .name       = "IndexBuffer-Nuklear",
        .queue_type = BUFFER_QUEUE_GRAPHIC,
        .usage      = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    };

    backend->res_vertex = rm_create_buffer(rm, &vert_info);
    backend->res_index  = rm_create_buffer(rm, &index_info);

    RBuffer *vtx_buffer =     rm_get_buffer(rm, backend->res_vertex);
    RBuffer *idx_buffer =     rm_get_buffer(rm, backend->res_index);

    vmaMapMemory(dev->allocator, vtx_buffer->alloc, &backend->mapped_vtx);
    vmaMapMemory(dev->allocator, idx_buffer->alloc, &backend->mapped_idx);


    

    // Optional: init Nuklear per-frame scratch buffers if you want persistent ones
    nk_buffer_init(&backend->vbuf, &backend->allocator, 4 * 1024);
    nk_buffer_init(&backend->ebuf, &backend->allocator, 4 * 1024);

  GpConfig config =    gp_init("Pipeline-UI");
  gp_set_color_formats(&config, &window->swapchain.format, 1);
  gp_set_topology(&config, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  
backend->pipeline =    pr_build_reg(hotreloader, &config, "shaders/ui.vert", "shaders/ui.frag");
return backend;
}

void nuklear_backend_new_frame(NuklearBackend *ctx, Input *input) { nuklear_backend_tick(&ctx->ctx, input); }

struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self) { return &self->ctx; }

void nuklear_backend_record(
    NuklearBackend *backend,
    CmdBuffer cmd,
    M_Pipeline *m_pipeline,
    M_Resource *rm,
    ResHandle swap_img,
    WindowRect win,
    M_GPU *dev)
{
    // 0) Setup projection uniform (same as you did)
    float projection[16] = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f,-2.0f, 0.0f, 0.0f,
        0.0f, 0.0f,-1.0f, 0.0f,
       -1.0f, 1.0f, 0.0f, 1.0f,
    };
    projection[0] /= (float)win.size.width;
    projection[5] /= (float)win.size.height;

    // TODO: upload projection to your uniform buffer / push constants here

    // 1) Begin rendering to swap_img (your engine call)
    RenderingBeginInfo begin = {
        .colors = &swap_img,
        .colors_count = 1,
        .h = win.size.height, .w = win.size.width,
        .offset_x = win.offset.x, .offset_y = win.offset.y,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD
    };
    cmd_begin_rendering(cmd, rm, &begin);

    // 2) Convert Nuklear draw list -> vertices/indices
    // You need mapped pointers to your host-visible buffers:
    void *mapped_vtx = backend->mapped_vtx; // <--- your function
    void *mapped_idx = backend->mapped_idx; // <--- your function

    _ensure_ui_buffers_size(backend, rm, dev);

    u64 max_vtx = rm_get_buffer(rm, backend->res_vertex)->capacity;
   u64 max_idx =   rm_get_buffer(rm, backend->res_index)->capacity;

    struct nk_buffer vbuf, ebuf;
    nk_buffer_init_fixed(&vbuf, mapped_vtx, max_vtx);
    nk_buffer_init_fixed(&ebuf, mapped_idx, max_idx);

    struct nk_convert_config cfg;
    NK_MEMSET(&cfg, 0, sizeof(cfg));
    cfg.vertex_layout = NK_VERTEX_LAYOUT;
    cfg.vertex_size = sizeof(GPUNuklearVertex);
    cfg.vertex_alignment = NK_ALIGNOF(GPUNuklearVertex);
    cfg.circle_segment_count = 22;
    cfg.curve_segment_count  = 22;
    cfg.arc_segment_count    = 22;
    cfg.global_alpha = 1.0f;
    cfg.shape_AA = NK_ANTI_ALIASING_OFF;
    cfg.line_AA  = NK_ANTI_ALIASING_OFF;

    nk_convert(&backend->ctx, &backend->nk_cmds, &vbuf, &ebuf, &cfg);

    // 3) Bind pipeline, vertex/index buffers
    u32 vb_descriptor_index = rm_get_buffer_descriptor_index(rm, backend->res_vertex);
    VkBuffer ib = rm_get_buffer(rm, backend->res_index)->handle;

    VkDeviceSize off = 0;
    vkCmdBindIndexBuffer(cmd.buffer, ib, 0, VK_INDEX_TYPE_UINT16);

    GPUPushUI ui_push = {};
    memcpy(ui_push.projection, projection, sizeof(projection));
    ui_push.screen_size[0] = (float)win.size.width;
    ui_push.screen_size[1] = (float)win.size.height;
    ui_push.tex_id = rm_get_image_descriptor_index(rm, backend->res_font);
    ui_push.vert_id = rm_get_buffer_index(rm, backend->res_vertex);

    BindPipelineInfo info = {.handle = backend->pipeline, .push_size = sizeof(GPUPushUI), .p_push = &ui_push};
    cmd_bind_pipeline(cmd, m_pipeline, rm, &info);

    const struct nk_draw_command *dcmd;
    uint32_t index_offset = 0;

    nk_draw_foreach(dcmd, &backend->ctx, &backend->nk_cmds) {
        if (!dcmd->elem_count) continue;
        if (!dcmd->texture.ptr) continue;

        // dcmd->texture.ptr is expected to be a VkImageView (as set via nk_handle_ptr)
        VkImageView view = (VkImageView)dcmd->texture.ptr;

        // Bind per-texture descriptor set for 'view' (your cache/update logic)
        // bind_texture_set(view);

        VkRect2D scissor;
        scissor.offset.x = (int32_t)NK_MAX(dcmd->clip_rect.x, 0.0f);
        scissor.offset.y = (int32_t)NK_MAX(dcmd->clip_rect.y, 0.0f);
        scissor.extent.width  = (uint32_t)dcmd->clip_rect.w;
        scissor.extent.height = (uint32_t)dcmd->clip_rect.h;

        vkCmdSetScissor(cmd.buffer, 0, 1, &scissor);

        vkCmdDrawIndexed(cmd.buffer, dcmd->elem_count, 1, index_offset, 0, 0);
        index_offset += dcmd->elem_count;
    }

    // 5) End + clear nuklear for next frame
    cmd_end_rendering(cmd);
    nk_clear(&backend->ctx);

}

void _ensure_ui_buffers_size(NuklearBackend* b, M_Resource* rm, M_GPU* dev)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        void* mapped_vtx = b->mapped_vtx;
        void* mapped_idx = b->mapped_idx;
        size_t vtx_cap = rm_get_buffer(rm, b->res_vertex)->capacity;
        size_t idx_cap = rm_get_buffer(rm, b->res_index)->capacity;

        int rc = _convert_try(b, rm, mapped_vtx, vtx_cap, mapped_idx, idx_cap);
        if (rc == NK_CONVERT_SUCCESS) return;

        
        rm_resize_buffer(rm, dev, vtx_cap * 2, b->res_vertex); 
        rm_resize_buffer(rm, dev, vtx_cap * 2, b->res_index); 
    }

    // If still failing, you’re way too small or you have a bug.
}

static int _convert_try(NuklearBackend* b, M_Resource* rm,
                       void* mapped_vtx, size_t vtx_cap,
                       void* mapped_idx, size_t idx_cap)
{
    struct nk_buffer vbuf, ebuf;
    nk_buffer_init_fixed(&vbuf, mapped_vtx, vtx_cap);
    nk_buffer_init_fixed(&ebuf, mapped_idx, idx_cap);

    struct nk_convert_config cfg = {0};
    cfg.vertex_layout = NK_VERTEX_LAYOUT;
    cfg.vertex_size = sizeof(GPUNuklearVertex);
    cfg.vertex_alignment = NK_ALIGNOF(GPUNuklearVertex);
    cfg.shape_AA = NK_ANTI_ALIASING_OFF;
    cfg.line_AA  = NK_ANTI_ALIASING_OFF;

    return nk_convert(&b->ctx, &b->nk_cmds, &vbuf, &ebuf, &cfg);
}
//  struct nk_glfw_device *dev = &glfw.vulkan;
//     struct nk_buffer vbuf, ebuf;

//     struct Mat4f projection = {
//         {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f,
//          0.0f, -1.0f, 1.0f, 0.0f, 1.0f},
//     };

//     VkCommandBufferBeginInfo begin_info;
//     VkClearValue clear_value = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
//     VkRenderPassBeginInfo render_pass_begin_nfo;
//     VkCommandBuffer command_buffer;
//     VkResult result;
//     VkViewport viewport;

//     VkDeviceSize doffset = 0;
//     VkImageView current_texture = NULL;
//     uint32_t index_offset = 0;
//     VkRect2D scissor;
//     uint32_t wait_semaphore_count;
//     VkSemaphore *wait_semaphores;
//     VkPipelineStageFlags wait_stage =
//         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
//     VkSubmitInfo submit_info;

//     projection.m[0] /= glfw.width;
//     projection.m[5] /= glfw.height;

//     memcpy(dev->mapped_uniform, &projection, sizeof(projection));

//     memset(&begin_info, 0, sizeof(VkCommandBufferBeginInfo));
//     begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

//     memset(&render_pass_begin_nfo, 0, sizeof(VkRenderPassBeginInfo));
//     render_pass_begin_nfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
//     render_pass_begin_nfo.renderPass = dev->render_pass;
//     render_pass_begin_nfo.renderArea.extent.width = (uint32_t)glfw.width;
//     render_pass_begin_nfo.renderArea.extent.height = (uint32_t)glfw.height;
//     render_pass_begin_nfo.clearValueCount = 1;
//     render_pass_begin_nfo.pClearValues = &clear_value;
//     render_pass_begin_nfo.framebuffer = dev->framebuffers[buffer_index];

//     command_buffer = dev->command_buffers[buffer_index];

//     result = vkBeginCommandBuffer(command_buffer, &begin_info);
//     NK_ASSERT(result == VK_SUCCESS);
//     vkCmdBeginRenderPass(command_buffer, &render_pass_begin_nfo,
//                          VK_SUBPASS_CONTENTS_INLINE);

//     memset(&viewport, 0, sizeof(VkViewport));
//     viewport.width = (float)glfw.width;
//     viewport.height = (float)glfw.height;
//     viewport.maxDepth = 1.0f;
//     vkCmdSetViewport(command_buffer, 0, 1, &viewport);

//     vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                       dev->pipeline);
//     vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                             dev->pipeline_layout, 0, 1,
//                             &dev->uniform_descriptor_set, 0, NULL);
//     {
//         /* convert from command queue into draw list and draw to screen */
//         const struct nk_draw_command *cmd;
//         /* load draw vertices & elements directly into vertex + element buffer
//          */
//         {
//             /* fill convert configuration */
//             struct nk_convert_config config;
//             static const struct nk_draw_vertex_layout_element vertex_layout[] =
//                 {{NK_VERTEX_POSITION, NK_FORMAT_FLOAT,
//                   NK_OFFSETOF(struct nk_glfw_vertex, position)},
//                  {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT,
//                   NK_OFFSETOF(struct nk_glfw_vertex, uv)},
//                  {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
//                   NK_OFFSETOF(struct nk_glfw_vertex, col)},
//                  {NK_VERTEX_LAYOUT_END}};
//             NK_MEMSET(&config, 0, sizeof(config));
//             config.vertex_layout = vertex_layout;
//             config.vertex_size = sizeof(struct nk_glfw_vertex);
//             config.vertex_alignment = NK_ALIGNOF(struct nk_glfw_vertex);
//             config.tex_null = dev->tex_null;
//             config.circle_segment_count = 22;
//             config.curve_segment_count = 22;
//             config.arc_segment_count = 22;
//             config.global_alpha = 1.0f;
//             config.shape_AA = AA;
//             config.line_AA = AA;

//             /* setup buffers to load vertices and elements */
//             nk_buffer_init_fixed(&vbuf, dev->mapped_vertex,
//                                  (size_t)dev->max_vertex_buffer);
//             nk_buffer_init_fixed(&ebuf, dev->mapped_index,
//                                  (size_t)dev->max_element_buffer);
//             nk_convert(&glfw.ctx, &dev->cmds, &vbuf, &ebuf, &config);
//         }

//         /* iterate over and execute each draw command */

//         vkCmdBindVertexBuffers(command_buffer, 0, 1, &dev->vertex_buffer,
//                                &doffset);
//         vkCmdBindIndexBuffer(command_buffer, dev->index_buffer, 0,
//                              VK_INDEX_TYPE_UINT16);

//         nk_draw_foreach(cmd, &glfw.ctx, &dev->cmds) {
//             if (!cmd->texture.ptr) {
//                 continue;
//             }
//             if (cmd->texture.ptr && cmd->texture.ptr != current_texture) {
//                 int found = 0;
//                 uint32_t i;
//                 for (i = 0; i < dev->texture_descriptor_sets_len; i++) {
//                     if (dev->texture_descriptor_sets[i].image_view ==
//                         cmd->texture.ptr) {
//                         found = 1;
//                         break;
//                     }
//                 }

//                 if (!found) {
//                     update_texture_descriptor_set(
//                         dev, &dev->texture_descriptor_sets[i],
//                         (VkImageView)cmd->texture.ptr);
//                     dev->texture_descriptor_sets_len++;
//                 }
//                 vkCmdBindDescriptorSets(
//                     command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
//                     dev->pipeline_layout, 1, 1,
//                     &dev->texture_descriptor_sets[i].descriptor_set, 0, NULL);
//             }

//             if (!cmd->elem_count)
//                 continue;

//             scissor.offset.x = (int32_t)(NK_MAX(cmd->clip_rect.x, 0.f) * glfw.fb_scale.x);
//             scissor.offset.y = (int32_t)(NK_MAX(cmd->clip_rect.y, 0.f) * glfw.fb_scale.y);
//             scissor.extent.width = (uint32_t)(cmd->clip_rect.w * glfw.fb_scale.x);
//             scissor.extent.height = (uint32_t)(cmd->clip_rect.h * glfw.fb_scale.y);
//             vkCmdSetScissor(command_buffer, 0, 1, &scissor);
//             vkCmdDrawIndexed(command_buffer, cmd->elem_count, 1, index_offset,
//                              0, 0);
//             index_offset += cmd->elem_count;
//         }
//         nk_clear(&glfw.ctx);
//     }

//     vkCmdEndRenderPass(command_buffer);
//     result = vkEndCommandBuffer(command_buffer);
//     NK_ASSERT(result == VK_SUCCESS);

//     if (wait_semaphore) {
//         wait_semaphore_count = 1;
//         wait_semaphores = &wait_semaphore;
//     } else {
//         wait_semaphore_count = 0;
//         wait_semaphores = NULL;
//     }

//     memset(&submit_info, 0, sizeof(VkSubmitInfo));
//     submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//     submit_info.commandBufferCount = 1;
//     submit_info.pCommandBuffers = &command_buffer;
//     submit_info.pWaitDstStageMask = &wait_stage;
//     submit_info.waitSemaphoreCount = wait_semaphore_count;
//     submit_info.pWaitSemaphores = wait_semaphores;
//     submit_info.signalSemaphoreCount = 1;
//     submit_info.pSignalSemaphores = &dev->render_completed;

//     result = vkQueueSubmit(graphics_queue, 1, &submit_info, NULL);
//     NK_ASSERT(result == VK_SUCCESS);

//     return dev->render_completed;
// --- Private Functions ---

static void nuklear_backend_tick(struct nk_context *nk, Input *in) {
  nk_input_begin(nk);

  for (uint32_t i = 0; i < in->text_len; ++i) {
    nk_input_unicode(nk, in->text[i]);
  }

  // Mouse position
  nk_input_motion(nk, (int)in->mouse_x, (int)in->mouse_y);

  // Mouse buttons (Nuklear wants position too)
  nk_input_button(nk, NK_BUTTON_LEFT, (int)in->mouse_x, (int)in->mouse_y,
                  input_button_down(in, GLFW_MOUSE_BUTTON_LEFT));
  nk_input_button(nk, NK_BUTTON_RIGHT, (int)in->mouse_x, (int)in->mouse_y,
                  input_button_down(in, GLFW_MOUSE_BUTTON_RIGHT));
  nk_input_button(nk, NK_BUTTON_MIDDLE, (int)in->mouse_x, (int)in->mouse_y,
                  input_button_down(in, GLFW_MOUSE_BUTTON_MIDDLE));

  // Modifiers (poll held)
  bool shift = input_key_down(in, GLFW_KEY_LEFT_SHIFT) || input_key_down(in, GLFW_KEY_RIGHT_SHIFT);
  bool ctrl = input_key_down(in, GLFW_KEY_LEFT_CONTROL) || input_key_down(in, GLFW_KEY_RIGHT_CONTROL);

  nk_input_key(nk, NK_KEY_SHIFT, shift);
  // Nuklear also uses ctrl internally for shortcuts; some builds have NK_KEY_CTRL, some don’t.
  // If your nuklear.h defines NK_KEY_CTRL you can set it, otherwise just gate shortcuts below.
#ifdef NK_KEY_CTRL
  nk_input_key(nk, NK_KEY_CTRL, ctrl);
#endif

  // Navigation / editing keys
  nk_input_key(nk, NK_KEY_DEL, input_key_down(in, GLFW_KEY_DELETE));
  nk_input_key(nk, NK_KEY_ENTER, input_key_down(in, GLFW_KEY_ENTER) || input_key_down(in, GLFW_KEY_KP_ENTER));
  nk_input_key(nk, NK_KEY_TAB, input_key_down(in, GLFW_KEY_TAB));
  nk_input_key(nk, NK_KEY_BACKSPACE, input_key_down(in, GLFW_KEY_BACKSPACE));
  nk_input_key(nk, NK_KEY_UP, input_key_down(in, GLFW_KEY_UP));
  nk_input_key(nk, NK_KEY_DOWN, input_key_down(in, GLFW_KEY_DOWN));
  nk_input_key(nk, NK_KEY_LEFT, input_key_down(in, GLFW_KEY_LEFT));
  nk_input_key(nk, NK_KEY_RIGHT, input_key_down(in, GLFW_KEY_RIGHT));

  // Word-left / word-right typically require ctrl
  nk_input_key(nk, NK_KEY_TEXT_WORD_LEFT, ctrl && input_key_down(in, GLFW_KEY_LEFT));
  nk_input_key(nk, NK_KEY_TEXT_WORD_RIGHT, ctrl && input_key_down(in, GLFW_KEY_RIGHT));

  // Line start/end
  nk_input_key(nk, NK_KEY_TEXT_LINE_START, input_key_down(in, GLFW_KEY_HOME));
  nk_input_key(nk, NK_KEY_TEXT_LINE_END, input_key_down(in, GLFW_KEY_END));

  // Optional: copy/paste/cut/undo/redo/select-all (edge is nicer, but "down" works)
  nk_input_key(nk, NK_KEY_COPY, ctrl && input_key_pressed(in, GLFW_KEY_C));
  nk_input_key(nk, NK_KEY_PASTE, ctrl && input_key_pressed(in, GLFW_KEY_V));
  nk_input_key(nk, NK_KEY_CUT, ctrl && input_key_pressed(in, GLFW_KEY_X));
  nk_input_key(nk, NK_KEY_TEXT_UNDO, ctrl && input_key_pressed(in, GLFW_KEY_Z));
  nk_input_key(nk, NK_KEY_TEXT_REDO, ctrl && input_key_pressed(in, GLFW_KEY_R));
  nk_input_key(nk, NK_KEY_TEXT_SELECT_ALL, ctrl && input_key_pressed(in, GLFW_KEY_A));

  // If you have scroll (add it), do: nk_input_scroll(nk, nk_vec2(0, scroll_y));

  nk_input_end(nk);
}
