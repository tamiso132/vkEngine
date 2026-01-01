
#include "raytrace_sample.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "resmanager.h"
#include "shaders/triangle.glsl"
#include <vulkan/vulkan_core.h>

typedef struct RaytraceData {
  PipelineHandle cs_pipeline;
  ResHandle cs_output_img;
} RaytraceData;

// --- Private Prototypes ---
static void _raytrace_init(Sample *self, SampleContext *ctx);
static void _render(Sample *self, SampleContext *ctx);
static void __resize(Sample *self, SampleContext *ctx);
static void __destroy(Sample *self, Managers *mg);

Sample create_raytrace_sample() {}

// --- Private Functions ---

static void _raytrace_init(Sample *self, SampleContext *ctx) {
  RaytraceData data = {};

  CpConfig config = cp_init("Raytrace Pipeline");
  cp_set_shader_path(&config, "shaders/triangle_compute.comp");
  data.cs_pipeline = pr_build_reg_cs(ctx->mg->reloader, config);
  RGImageInfo info = {.name = "IMG_Raytrace_Output",
                      .format = ctx->swapchain->format,
                      .height = ctx->swapchain->extent.height,
                      .width = ctx->swapchain->extent.width,
                      .usage = VK_IMAGE_USAGE_STORAGE_BIT};

  data.cs_output_img = rm_create_image(ctx->mg->rm, info);
}

static void _render(Sample *self, SampleContext *ctx) {
  uint32_t groupsX = (ctx->extent.width + 7) / 8;
  uint32_t groupsY = (ctx->extent.height + 7) / 8;
}

static void __resize(Sample *self, SampleContext *ctx) {}

static void __destroy(Sample *self, Managers *mg) {}
