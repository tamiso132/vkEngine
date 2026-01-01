#include "command.h"
#include "common.h"
#include "gpu/gpu.h"
#include "gpu/swapchain.h"
#include "sample_interface.h"
#include <stdlib.h>

#include "shaders/triangle.glsl"
#include "triangle_sample.h"
// Definiera vertex-struktur
typedef struct {
  float x, y, z, p;
} TriangleVertex;

const TriangleVertex vertices[] = {{0.0f, -0.5f, 0.0f, 1.0f}, {0.5f, 0.5f, 0.0f, 1.0f}, {-0.5f, 0.5f, 0.0f, 1.0f}};

// Privat state för detta sample
typedef struct {
  PipelineHandle pipeline;
  ResHandle vbo;
} TriangleData;

// --- Private Prototypes ---

// --- Implementering ---

void tri_init(Sample *self, SampleContext *ctx) {
  TriangleData *data = calloc(1, sizeof(TriangleData));
  self->user_data = data;
  Managers *mg = ctx->mg;
  GPUDevice *device = ctx->device;

  const char *vs_path = "shaders/triangle.vert";
  const char *fs_path = "shaders/triangle.frag";

  GpConfig b = gp_init(mg->rm, "TrianglePipeline");
  gp_set_topology(&b, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gp_set_cull(&b, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gp_set_color_formats(&b, &ctx->swapchain->format, 1);

  PipelineHandle triangle_pip = pr_build_reg(mg->reloader, &b, vs_path, fs_path);

  RGBufferInfo buffer_info = {.capacity = sizeof(TriangleVertex) * 12,
                              .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                              .name = "TriangleVertices",
                              .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT};

  data->vbo = rm_create_buffer(mg->rm, &buffer_info);
  cmd_buffer_upload(ctx->cmd, mg->rm, data->vbo, (void *)vertices, sizeof(TriangleVertex) * 12);
}

void tri_render(Sample *self, SampleContext *ctx) {

  // Begin Rendering (Dynamic Rendering)
  ResHandle img = swapchain_get_image(ctx->swapchain);
  RenderingBeginInfo begin_info = {
      .colors = &img,
      .colors_count = 1,
      .w = ctx->swapchain->extent.width,
      .h = ctx->swapchain->extent.height,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clear_color = {0.05f, 0.05f, 0.05f} // Snygg mörkgrå
  };
  cmd_begin_rendering(ctx->cmd, ctx->mg->rm, &begin_info);

  TriangleData *data = (TriangleData *)self->user_data;

  PushTriangle push = {.vertex_id = rm_get_buffer_descriptor_index(ctx->mg->rm, data->vbo)};

  BindPipelineInfo bind = {.handle = data->pipeline, .p_push = &push, .push_size = sizeof(PushTriangle)};

  cmd_bind_pipeline(ctx->cmd, ctx->mg->pm, &bind);
  vkCmdDraw(ctx->cmd.buffer, 3, 1, 0, 0);

  cmd_end_rendering(ctx->cmd);
}

void tri_resize(Sample *self, SampleContext *ctx) {}

void tri_destroy(Sample *self, Managers *mg) {
  TriangleData *data = (TriangleData *)self->user_data;
  free(data);
}

// Public Factory Function
Sample create_triangle_sample() {
  return (Sample){.name = "Triangle Hello World",
                  .init = tri_init,
                  .render = tri_render,
                  .on_resize = tri_resize,
                  .destroy = tri_destroy};
}
// --- Private Functions ---
