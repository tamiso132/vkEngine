#include "ui/clay_backend.h" // Ensure this path matches your project structure
#include "cglm/vec4.h"
#include "command.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/shader_compiler.h"
#include "gpu/swapchain.h"
#include "resource/resmanager.h"
#include "shaders/ui_shared.glsl" //
#include <string.h>               // memcpy
#include <vulkan/vulkan_core.h>

// --- Private Prototypes ---
static void ClayErrorCallback(Clay_ErrorData errorData);

static void upload_white_pixel(CmdBuffer cmd, M_GPU *gpu, M_Resource *rm, ResHandle texture);
static Clay_Dimensions ClayMeasureTextFallback(Clay_StringSlice text,
                                              Clay_TextElementConfig* config,
                                              void* userData);

// --- Public Functions ---

void clay_backend_init(ClayContext *ctx, M_GPU *gpu, M_Resource *rm, M_HotReload *pr, CmdBuffer cmd, VkFormat format,
                       uint32_t width, uint32_t height) {
  memset(ctx, 0, sizeof(ClayContext));
  ctx->gpu = gpu;
  ctx->rm = rm;
  ctx->pr = pr;
  ctx->max_vertices = 20000;
  ctx->max_indices = 60000;

  // --- 1. Initialize Clay Library (Encapsulated) ---
  uint64_t claySize = Clay_MinMemorySize();
  ctx->internal_memory = malloc(claySize);

  if (!ctx->internal_memory) {
    printf("[UI] Fatal: Failed to allocate Clay memory.\n");
    return;
  }

  Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(claySize, ctx->internal_memory);
  Clay_Initialize(arena, (Clay_Dimensions){(float)width, (float)height},
                  (Clay_ErrorHandler){.errorHandlerFunction = ClayErrorCallback});

  Clay_SetMeasureTextFunction(ClayMeasureTextFallback, ctx);

  // --- 2. Initialize Vulkan Resources ---

  // Vertex Pulling Buffer (Storage)
  RGBufferInfo vb_info = {.name = "Clay_Vertex_SSBO",
                          .capacity = ctx->max_vertices * sizeof(GPUClayVertex),
                          .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          .queue_type = BUFFER_QUEUE_GRAPHIC};
  ctx->vtx_buffer = rm_create_buffer(rm, &vb_info);

  // Index Buffer
  RGBufferInfo ib_info = {.name = "Clay_Index_Buffer",
                          .capacity = ctx->max_indices * sizeof(uint16_t),
                          .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          .queue_type = BUFFER_QUEUE_GRAPHIC};
  ctx->idx_buffer = rm_create_buffer(rm, &ib_info);

  // White Pixel Texture
  RGImageInfo img_info = {.name = "Clay_White_Pixel",
                          .width = 1,
                          .height = 1,
                          .format = VK_FORMAT_R8G8B8A8_UNORM,
                          .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          .preset = RG_IMAGETYPE_TEXTURE};
  ctx->font_texture = rm_create_image(rm, img_info);
  upload_white_pixel(cmd, gpu, rm, ctx->font_texture);

  // Pipeline
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

void clay_backend_set_font_texture(ClayContext *ctx, ResHandle texture_handle) { ctx->font_texture = texture_handle; }

void clay_backend_shutdown(ClayContext *ctx) {
  if (ctx->internal_memory) {
    free(ctx->internal_memory);
    ctx->internal_memory = NULL;
  }
}
void clay_backend_render(ClayContext *ctx, CmdBuffer cmd, M_Resource* rm, M_Swapchain * swap, uint32_t width,
                         uint32_t height) {

  
  ResHandle main_img = swapchain_get_image(swap);

  cmd_sync_image(cmd, rm, main_img, STATE_COLOR, ACCESS_WRITE);
      
  RenderingBeginInfo debug_begin = {.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .h =height, 
  .w = width, .colors = &main_img, .colors_count = 1};

  cmd_begin_rendering(cmd, rm, &debug_begin);

  Clay_RenderCommandArray command = Clay_EndLayout();
  Clay_RenderCommandArray* commands  = &command;
  
  if (commands->length == 0)
    return;

  // --- 1. Map Buffers ---
  RBuffer *r_vtx = rm_get_buffer(ctx->rm, ctx->vtx_buffer);
  RBuffer *r_idx = rm_get_buffer(ctx->rm, ctx->idx_buffer);

  GPUClayVertex *vtx_ptr;
  uint16_t *idx_ptr;

  vmaMapMemory(ctx->gpu->allocator, r_vtx->alloc, (void **)&vtx_ptr);
  vmaMapMemory(ctx->gpu->allocator, r_idx->alloc, (void **)&idx_ptr);

  // --- 2. Generate Geometry ---
  uint32_t v_offset = 0;
  uint32_t i_offset = 0;

  for (int i = 0; i < commands->length; i++) {
    Clay_RenderCommand *rc = Clay_RenderCommandArray_Get(commands, i);

    if (rc->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
      Clay_BoundingBox box = rc->boundingBox;
      Clay_Color c = rc->renderData.rectangle.backgroundColor;

      vec4 color = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};

      // Indices
      idx_ptr[i_offset + 0] = v_offset + 0;
      idx_ptr[i_offset + 1] = v_offset + 1;
      idx_ptr[i_offset + 2] = v_offset + 2;
      idx_ptr[i_offset + 3] = v_offset + 2;
      idx_ptr[i_offset + 4] = v_offset + 3;
      idx_ptr[i_offset + 5] = v_offset + 0;

      // Vertices
      vtx_ptr[v_offset + 0] = (GPUClayVertex){{box.x, box.y}, {0.5f, 0.5f}};
      vtx_ptr[v_offset + 1] = (GPUClayVertex){{box.x + box.width, box.y}, {0.5f, 0.5f}};
      vtx_ptr[v_offset + 2] = (GPUClayVertex){{box.x + box.width, box.y + box.height}, {0.5f, 0.5f}};
      vtx_ptr[v_offset + 3] = (GPUClayVertex){{box.x, box.y + box.height}, {0.5f, 0.5f}};

      glm_vec4_copy(color, vtx_ptr[v_offset + 0].color);
      glm_vec4_copy(color, vtx_ptr[v_offset + 1].color);
      glm_vec4_copy(color, vtx_ptr[v_offset + 2].color);
      glm_vec4_copy(color, vtx_ptr[v_offset + 3].color);

      v_offset += 4;
      i_offset += 6;
    }
    // TODO: Handle Text
  }

  vmaUnmapMemory(ctx->gpu->allocator, r_vtx->alloc);
  vmaUnmapMemory(ctx->gpu->allocator, r_idx->alloc);

  // --- 3. Record Commands ---
  // Bind Pipeline
  GPUPushUI push_ui = {.screen_size = {width, height}, .tex_id = rm_get_image_index(ctx->rm,ctx->font_texture), .vert_id = rm_get_buffer_index(ctx->rm, ctx->vtx_buffer)};
 
  M_Pipeline *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  BindPipelineInfo pipe_info = {.handle = ctx->pipeline,
                                // We set Push Constants manually below
                                .p_push = &push_ui,
                                .push_size = sizeof(GPUPushUI)};
  cmd_bind_pipeline(cmd, pm, ctx->rm, &pipe_info);

  // Bind Index Buffer ONLY
  vkCmdBindIndexBuffer(cmd.buffer, r_idx->handle, 0, VK_INDEX_TYPE_UINT16);

  // Bind Global Resources (Bindless)
  cmd_bind_bindless(cmd, ctx->rm, (VkExtent2D){width, height});

  // Prepare Push Constants for Vertex Pulling
  uint32_t white_tex_id = rm_get_image_descriptor_index(ctx->rm, ctx->font_texture);
  uint32_t vtx_buf_id = rm_get_buffer_descriptor_index(ctx->rm, ctx->vtx_buffer);

  VkPipelineLayout layout = rm_get_pipeline_layout(ctx->rm);
  VkShaderStageFlags stages =  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

  // Draw
  if (i_offset > 0) {
    GPUPushUI pc = {.screen_size = {(float)width, (float)height}, .tex_id = white_tex_id, .vert_id = vtx_buf_id};
    vkCmdPushConstants(cmd.buffer, layout, stages, 0, sizeof(GPUPushUI), &pc);
    vkCmdDrawIndexed(cmd.buffer, i_offset, 1, 0, 0, 0);
  }

  cmd_end_rendering(cmd);

    cmd_sync_image(cmd, rm, main_img, STATE_PRESENT, ACCESS_READ);
}

// --- Private Functions ---

// --- Internal Types ---
static void ClayErrorCallback(Clay_ErrorData errorData) {
  printf("[CLAY ERROR] %.*s\n", errorData.errorText.length, errorData.errorText.chars);
}

static void upload_white_pixel(CmdBuffer cmd, M_GPU *gpu, M_Resource *rm, ResHandle texture) {
  uint32_t pixel = 0xFFFFFFFF;
  RmStageSlice slice = rm_get_stage_buffer(rm, &pixel, 4, 4);

  cmd_sync_image(cmd, rm, texture, STATE_TRANSFER, ACCESS_WRITE);

  RImage *ri = rm_get_image(rm, texture);
  VkBufferImageCopy region = {
      .bufferOffset = slice.offset, .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {1, 1, 1}};
  vkCmdCopyBufferToImage(cmd.buffer, slice.buffer, ri->handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  cmd_sync_image(cmd, rm, texture, STATE_SHADER, ACCESS_READ);
}


static uint32_t utf8_count_codepoints(const char* s, uint32_t len) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if ((c & 0xC0) != 0x80) count++; // not a continuation byte
  }
  return count;
}

static Clay_Dimensions ClayMeasureTextFallback(Clay_StringSlice text,
                                              Clay_TextElementConfig* config,
                                              void* userData)
{
  (void)userData;

  float fontSize = (float)config->fontSize;
  float lineH    = (config->lineHeight != 0) ? (float)config->lineHeight : fontSize;

  const char* ptr = (const char*)text.chars;
  uint32_t len    = (uint32_t)text.length;
  uint32_t glyphs = utf8_count_codepoints(ptr, len);

  // cheap but stable approximation until you wire real font metrics
  float avgGlyphW = fontSize * 0.60f;
  float spacing   = (float)config->letterSpacing;

  float width = 0.0f;
  if (glyphs) width = avgGlyphW * (float)glyphs + spacing * (float)(glyphs - 1);

  return (Clay_Dimensions){ width, lineH };
}
