
#include "raytrace_sample.h"
#include "command.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/swapchain.h"
#include "resmanager.h"
#include "sample_interface.h"
#include "shaders/triangle.glsl"
#include <vulkan/vulkan_core.h>

typedef struct RaytraceData {
  PipelineHandle cs_pipeline;
  ResHandle cs_output_img;
} RaytraceData;

// --- Private Prototypes ---
static void _init(Sample *self, SampleContext *ctx);
static void _render(Sample *self, SampleContext *ctx);
static void _resize(Sample *self, SampleContext *ctx);
static void _destroy(Sample *self, Managers *mg);

Sample create_raytrace_sample() { return (Sample){.init = _init, .render = _render, .on_resize = _resize}; }

// --- Private Functions ---

static void _init(Sample *self, SampleContext *ctx) {
  RaytraceData *data = calloc(sizeof(RaytraceData), 1);

  CpConfig config = cp_init("Raytrace Pipeline");
  cp_set_shader_path(&config, "shaders/triangle_compute.comp");

  RGImageInfo info = {.name = "IMG_Raytrace_Output",
                      .format = VK_FORMAT_B8G8R8A8_UNORM,
                      .height = ctx->swapchain->extent.height,
                      .width = ctx->swapchain->extent.width,
                      .usage = VK_IMAGE_USAGE_STORAGE_BIT};

  data->cs_pipeline = pr_build_reg_cs(ctx->mg->reloader, config);
  data->cs_output_img = rm_create_image(ctx->mg->rm, info);

  self->user_data = data;
}

static void _render(Sample *self, SampleContext *ctx) {
  RaytraceData *data = self->user_data;
  uint32_t groupsX = (ctx->extent.width + 7) / 8;
  uint32_t groupsY = (ctx->extent.height + 7) / 8;
  ResHandle swap_img = swapchain_get_image(ctx->swapchain);

  PushComputeTriangle p = {.extent = {ctx->extent.width, ctx->extent.height},
                           .img_id = rm_get_image_index(ctx->mg->rm, data->cs_output_img),
                           .data = {0.5, 0.5, 0.5, 1.0}};

  BindPipelineInfo b = {.p_push = &p, .push_size = sizeof(PushComputeTriangle), .handle = data->cs_pipeline};

  cmd_bind_pipeline(ctx->cmd, ctx->mg->pm, &b);

  cmd_sync_image(ctx->cmd, ctx->mg->rm, data->cs_output_img, STATE_SHADER, ACCESS_WRITE);
  vkCmdDispatch(ctx->cmd.buffer, groupsX, groupsY, 1);
  cmd_image_copy_to_image(ctx->cmd, ctx->mg->rm, data->cs_output_img, swap_img);
}

static void _resize(Sample *self, SampleContext *ctx) {
  RaytraceData *data = self->user_data;
  rm_resize_image(ctx->mg->rm, data->cs_output_img, ctx->extent.width, ctx->extent.height);
}

static void _destroy(Sample *self, Managers *mg) {}
