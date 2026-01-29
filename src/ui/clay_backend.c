#include "ui/clay_backend.h"
#include "ui_internal.h" // Includes font function declarations

#include "cglm/vec4.h"
#include "command.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "resource/resmanager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

// ----------------------------
// Config
// ----------------------------
#ifndef UI_FONT_TTF_PATH
#define UI_FONT_TTF_PATH "assets/ttf/16020_FUTURAM.ttf"
#endif

#define UI_FONT_ATLAS_W 512
#define UI_FONT_ATLAS_H 512
#define UI_FONT_BAKE_PX 32.0f

// ----------------------------
// Private types
// ----------------------------

typedef struct UI_Batch {
  uint32_t first_index;
  uint32_t index_count;
  uint32_t tex_id;     // bindless descriptor index
  VkRect2D scissor;
} UI_Batch;

// ----------------------------
// Private prototypes
// ----------------------------
static void ClayErrorCallback(Clay_ErrorData errorData);
static void upload_white_pixel(CmdBuffer cmd, M_GPU *gpu, M_Resource *rm, ResHandle texture);

static void emit_rect(GPUClayVertex* vtx, uint16_t* idx,
                      uint32_t* v_off, uint32_t* i_off,
                      Clay_BoundingBox box, vec4 color);

static inline VkRect2D full_scissor(uint32_t w, uint32_t h) {
  VkRect2D r;
  r.offset.x = 0; r.offset.y = 0;
  r.extent.width = w; r.extent.height = h;
  return r;
}

// ----------------------------
// Public API
// ----------------------------

void clay_backend_init(ClayContext *ctx,
                       M_GPU *gpu,
                       M_Resource *rm,
                       M_HotReload *pr,
                       CmdBuffer cmd,
                       VkFormat format,
                       uint32_t width,
                       uint32_t height)
{
  memset(ctx, 0, sizeof(*ctx));
  ctx->gpu = gpu;
  ctx->rm  = rm;
  ctx->pr  = pr;
  ctx->max_vertices = 20000;
  ctx->max_indices  = 60000;

  // 1) Clay init
  uint64_t claySize = Clay_MinMemorySize();
  ctx->internal_memory = malloc(claySize);
  if (!ctx->internal_memory) {
    printf("[UI] Fatal: Failed to allocate Clay memory.\n");
    return;
  }

  Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(claySize, ctx->internal_memory);
  Clay_Initialize(arena,
                  (Clay_Dimensions){(float)width, (float)height},
                  (Clay_ErrorHandler){.errorHandlerFunction = ClayErrorCallback});

  // 2) Vulkan buffers
  RGBufferInfo vb_info = {
    .name = "Clay_Vertex_SSBO",
    .capacity = ctx->max_vertices * sizeof(GPUClayVertex),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    .queue_type = BUFFER_QUEUE_GRAPHIC
  };
  ctx->vtx_buffer = rm_create_buffer(rm, &vb_info);

  RGBufferInfo ib_info = {
    .name = "Clay_Index_Buffer",
    .capacity = ctx->max_indices * sizeof(uint16_t),
    .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    .queue_type = BUFFER_QUEUE_GRAPHIC
  };
  ctx->idx_buffer = rm_create_buffer(rm, &ib_info);

  // 3) White pixel texture (for rectangles/borders)
  RGImageInfo white_info = {
    .name = "Clay_White_Pixel",
    .width = 1,
    .height = 1,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .preset = RG_IMAGETYPE_TEXTURE
  };
  ctx->white_texture = rm_create_image(rm, white_info);
  upload_white_pixel(cmd, gpu, rm, ctx->white_texture);

  // Default font texture to white pixel until atlas is loaded
  ctx->font_texture = ctx->white_texture;

  // 4) Build font atlas using external helper from font.c
  if (ui_fontatlas_init_from_ttf_file(&ctx->font_cpu, 
                                      UI_FONT_TTF_PATH, 
                                      UI_FONT_BAKE_PX, 
                                      UI_FONT_ATLAS_W, UI_FONT_ATLAS_H, 
                                      cmd, rm)) 
  {
      // Update the font texture handle to point to the real atlas
      ctx->font_texture = ctx->font_cpu.texture;
  } else {
      printf("[UI] Warning: font atlas build failed; text will use white pixel.\n");
  }

  // 5) Text measurement for Clay layout using external helper from font.c
  Clay_SetMeasureTextFunction(ui_clay_measure_text_stb, &ctx->font_cpu);

  // 6) Pipeline
  GpConfig config = gp_init("Clay_Pipeline");
  gp_set_topology(&config, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gp_set_cull(&config, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gp_set_color_formats(&config, &format, 1);
  gp_enable_blend(&config);
  config.depth_test = false;
  config.depth_write = false;

  VkDescriptorSetLayout bindless_layout = rm_get_bindless_layout(rm);
  gp_set_layout(&config, bindless_layout, sizeof(GPUPushUI));

  ctx->pipeline = pr_build_reg(pr, &config, "shaders/ui.vert", "shaders/ui.frag");
}

void clay_backend_set_font_texture(ClayContext *ctx, ResHandle texture_handle) {
  ctx->font_texture = texture_handle;
}

void clay_backend_shutdown(ClayContext *ctx) {
  ui_fontatlas_shutdown(&ctx->font_cpu);
  
  if (ctx->internal_memory) {
    free(ctx->internal_memory);
    ctx->internal_memory = NULL;
  }
}

void clay_backend_render(ClayContext *ctx,
                         CmdBuffer cmd,
                         M_Resource* rm,
                         M_Swapchain *swap,
                         uint32_t width,
                         uint32_t height)
{
  ResHandle main_img = swapchain_get_image(swap);

  // transition to color
  cmd_sync_image(cmd, rm, main_img, STATE_COLOR, ACCESS_WRITE);

  RenderingBeginInfo begin = {
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    .h = height,
    .w = width,
    .colors = &main_img,
    .colors_count = 1
  };

  cmd_begin_rendering(cmd, rm, &begin);

  // IMPORTANT: user must have called Clay_BeginLayout() earlier in the frame.
  Clay_RenderCommandArray commands = Clay_EndLayout();

  // Always end rendering, even if no commands
  if (commands.length == 0) {
    cmd_end_rendering(cmd);
    cmd_sync_image(cmd, rm, main_img, STATE_PRESENT, ACCESS_READ);
    return;
  }

  // Map buffers
  RBuffer *r_vtx = rm_get_buffer(ctx->rm, ctx->vtx_buffer);
  RBuffer *r_idx = rm_get_buffer(ctx->rm, ctx->idx_buffer);

  GPUClayVertex *vtx_ptr = NULL;
  uint16_t *idx_ptr = NULL;

  vmaMapMemory(ctx->gpu->allocator, r_vtx->alloc, (void **)&vtx_ptr);
  vmaMapMemory(ctx->gpu->allocator, r_idx->alloc, (void **)&idx_ptr);

  uint32_t v_off = 0;
  uint32_t i_off = 0;

  // Batching: split when tex_id or scissor changes
  UI_Batch batches[2048];
  uint32_t batch_count = 0;

  VkRect2D cur_scissor = full_scissor(width, height);

  // texture ids:
  uint32_t white_tex_id = rm_get_image_descriptor_index(ctx->rm, ctx->white_texture);
  uint32_t font_tex_id  = 0;

  if (ctx->font_cpu.valid) {
    font_tex_id = rm_get_image_descriptor_index(ctx->rm, ctx->font_cpu.texture);
  } else {
    // fallback to white if atlas missing
    font_tex_id = white_tex_id;
  }

  uint32_t cur_tex_id = UINT32_MAX;

  // helper to start a new batch
  #define START_BATCH(tex) { \
    if (batch_count < 2048) { \
      batches[batch_count++] = (UI_Batch){ \
        .first_index = i_off, \
        .index_count = 0, \
        .tex_id = (tex), \
        .scissor = cur_scissor \
      }; \
      cur_tex_id = (tex); \
    } \
  }

  START_BATCH(white_tex_id);

  for (int i = 0; i < commands.length; i++) {
    Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(&commands, i);

    // scissor handling (Clay uses clip in newer versions)
    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
      VkRect2D s;
      s.offset.x = (int32_t)rc->boundingBox.x;
      s.offset.y = (int32_t)rc->boundingBox.y;
      s.extent.width  = (uint32_t)rc->boundingBox.width;
      s.extent.height = (uint32_t)rc->boundingBox.height;

      // new scissor => new batch
      if (memcmp(&s, &cur_scissor, sizeof(VkRect2D)) != 0) {
        cur_scissor = s;
        START_BATCH(cur_tex_id);
      }
      continue;
    }

    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
      VkRect2D s = full_scissor(width, height);
      if (memcmp(&s, &cur_scissor, sizeof(VkRect2D)) != 0) {
        cur_scissor = s;
        START_BATCH(cur_tex_id);
      }
      continue;
    }

    // RECTANGLE
    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
      if (cur_tex_id != white_tex_id) START_BATCH(white_tex_id);

      Clay_Color c = rc->renderData.rectangle.backgroundColor;
      vec4 color = {c.r/255.f, c.g/255.f, c.b/255.f, c.a/255.f};

      emit_rect(vtx_ptr, idx_ptr, &v_off, &i_off, rc->boundingBox, color);
      batches[batch_count - 1].index_count = i_off - batches[batch_count - 1].first_index;
      continue;
    }

    // BORDER (FIXED)
    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_BORDER) {
      if (cur_tex_id != white_tex_id) START_BATCH(white_tex_id);

      Clay_BorderRenderData* bd = &rc->renderData.border;
      Clay_Color cc = bd->color;
      vec4 color = {cc.r/255.f, cc.g/255.f, cc.b/255.f, cc.a/255.f};
      Clay_BoundingBox b = rc->boundingBox;

      float w_top    = (float)bd->width.top;
      float w_bottom = (float)bd->width.bottom;
      float w_left   = (float)bd->width.left;
      float w_right  = (float)bd->width.right;

      // Top (Full Width)
      if (w_top > 0)
        emit_rect(vtx_ptr, idx_ptr, &v_off, &i_off, 
          (Clay_BoundingBox){b.x, b.y, b.width, w_top}, color);
      
      // Bottom (Full Width)
      if (w_bottom > 0)
        emit_rect(vtx_ptr, idx_ptr, &v_off, &i_off, 
          (Clay_BoundingBox){b.x, b.y + b.height - w_bottom, b.width, w_bottom}, color);
      
      // Left (Inset by top/bottom to avoid overlap)
      if (w_left > 0)
        emit_rect(vtx_ptr, idx_ptr, &v_off, &i_off, 
          (Clay_BoundingBox){b.x, b.y + w_top, w_left, b.height - w_top - w_bottom}, color);

      // Right (Inset by top/bottom)
      if (w_right > 0)
        emit_rect(vtx_ptr, idx_ptr, &v_off, &i_off, 
          (Clay_BoundingBox){b.x + b.width - w_right, b.y + w_top, w_right, b.height - w_top - w_bottom}, color);

      batches[batch_count - 1].index_count = i_off - batches[batch_count - 1].first_index;
      continue;
    }

    // TEXT
    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
      if (cur_tex_id != font_tex_id) START_BATCH(font_tex_id);

      Clay_TextRenderData* td = &rc->renderData.text;

      // Use the function from font.c via ui_internal.h
      ui_clay_emit_text(&ctx->font_cpu,
                        td,
                        rc->boundingBox,
                        vtx_ptr, idx_ptr,
                        &v_off, &i_off);

      batches[batch_count - 1].index_count = i_off - batches[batch_count - 1].first_index;
      continue;
    }
  }

  vmaUnmapMemory(ctx->gpu->allocator, r_vtx->alloc);
  vmaUnmapMemory(ctx->gpu->allocator, r_idx->alloc);

  // Bind pipeline
  M_Pipeline *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);

  // Bind once (we’ll push constants per batch)
  GPUPushUI dummy = {0};
  BindPipelineInfo pipe_info = {
    .handle = ctx->pipeline,
    .p_push = &dummy,
    .push_size = sizeof(GPUPushUI)
  };
  cmd_bind_pipeline(cmd, pm, ctx->rm, &pipe_info);

  // Bind index buffer
  vkCmdBindIndexBuffer(cmd.buffer, r_idx->handle, 0, VK_INDEX_TYPE_UINT16);

  // Bind bindless set(s)
  cmd_bind_bindless(cmd, ctx->rm, (VkExtent2D){width, height});

  uint32_t vtx_buf_id = rm_get_buffer_descriptor_index(ctx->rm, ctx->vtx_buffer);

  VkPipelineLayout layout = rm_get_pipeline_layout(ctx->rm);
  VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

  // Draw each batch with its tex_id + scissor
  for (uint32_t b = 0; b < batch_count; b++) {
    UI_Batch* bt = &batches[b];
    if (bt->index_count == 0) continue;

    vkCmdSetScissor(cmd.buffer, 0, 1, &bt->scissor);

    GPUPushUI pc = {
      .screen_size = {(float)width, (float)height},
      .tex_id = bt->tex_id,
      .vert_id = vtx_buf_id
    };

    vkCmdPushConstants(cmd.buffer, layout, stages, 0, sizeof(GPUPushUI), &pc);

    vkCmdDrawIndexed(cmd.buffer,
                     bt->index_count,
                     1,
                     bt->first_index,
                     0,
                     0);
  }

  cmd_end_rendering(cmd);

  // transition to present
  cmd_sync_image(cmd, rm, main_img, STATE_PRESENT, ACCESS_READ);
}

// ----------------------------
// Private impl
// ----------------------------

static void ClayErrorCallback(Clay_ErrorData errorData) {
  printf("[CLAY ERROR] %.*s\n", errorData.errorText.length, errorData.errorText.chars);
}

static void upload_white_pixel(CmdBuffer cmd, M_GPU *gpu, M_Resource *rm, ResHandle texture) {
  uint32_t pixel = 0xFFFFFFFF;
  RmStageSlice slice = rm_get_stage_buffer(rm, &pixel, 4, 4);

  cmd_sync_image(cmd, rm, texture, STATE_TRANSFER, ACCESS_WRITE);

  RImage *ri = rm_get_image(rm, texture);
  VkBufferImageCopy region = {
    .bufferOffset = slice.offset,
    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
    .imageExtent = {1, 1, 1}
  };
  vkCmdCopyBufferToImage(cmd.buffer, slice.buffer, ri->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  cmd_sync_image(cmd, rm, texture, STATE_SHADER, ACCESS_READ);
}

static void emit_rect(GPUClayVertex* vtx, uint16_t* idx,
                      uint32_t* v_off, uint32_t* i_off,
                      Clay_BoundingBox box, vec4 color)
{
  uint32_t v = *v_off;
  uint32_t i = *i_off;

  idx[i + 0] = (uint16_t)(v + 0);
  idx[i + 1] = (uint16_t)(v + 1);
  idx[i + 2] = (uint16_t)(v + 2);
  idx[i + 3] = (uint16_t)(v + 2);
  idx[i + 4] = (uint16_t)(v + 3);
  idx[i + 5] = (uint16_t)(v + 0);

  // UVs point into "white pixel" (0.5,0.5) works with a 1x1 texture too
  vtx[v + 0] = (GPUClayVertex){{box.x, box.y}, {0.5f, 0.5f}};
  vtx[v + 1] = (GPUClayVertex){{box.x + box.width, box.y}, {0.5f, 0.5f}};
  vtx[v + 2] = (GPUClayVertex){{box.x + box.width, box.y + box.height}, {0.5f, 0.5f}};
  vtx[v + 3] = (GPUClayVertex){{box.x, box.y + box.height}, {0.5f, 0.5f}};

  glm_vec4_copy(color, vtx[v + 0].color);
  glm_vec4_copy(color, vtx[v + 1].color);
  glm_vec4_copy(color, vtx[v + 2].color);
  glm_vec4_copy(color, vtx[v + 3].color);

  *v_off += 4;
  *i_off += 6;
}