
#include "raytrace_sample.h"
#include "cglm/call/vec3.h"
#include "command.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "raycam.h"
#include "resource/resmanager.h"
#include "rt/rt_shared.glsl"
#include "sample_interface.h"
#include "shaders/rt/rt_shared.glsl"
#include "transfer_queue.h"
#include "world/world.h"

#include <math.h>

typedef struct RaytraceData {
  PipelineHandle cs_pipeline;
  ResHandle cs_output_img;
  ResHandle cam_buffer;
  World *world;
} RaytraceData;

// --- Private Prototypes ---
static void _init(Sample *self, SampleContext *ctx);
static void _render(Sample *self, SampleContext *ctx);
static void _resize(Sample *self, SampleContext *ctx);

Sample create_raytrace_sample() { return (Sample){.init = _init, .render = _render, .on_resize = _resize}; }

// --- Private Functions ---

static void _init(Sample *self, SampleContext *ctx) {
  RaytraceData *data = calloc(sizeof(RaytraceData), 1);
  WorldConfig w_config = {.chunk_size = CHUNK_SIZE, .visibility = 4};
  data->world = world_create(&w_config, ctx->cam.pos);
  world_init_gpu(data->world, ctx->rm, ctx->tq);

  CpConfig config = cp_init("Raytrace Pipeline");
  cp_set_shader_path(&config, "shaders/rt/rt.comp");

  RGImageInfo info = {.name = "IMG_Raytrace_Output",
                      .format = VK_FORMAT_B8G8R8A8_UNORM,
                      .height = ctx->extent.height,
                      .width = ctx->extent.width,
                      .usage = VK_IMAGE_USAGE_STORAGE_BIT};

  RGBufferInfo cam_info = {
      .name = "CamBuffer",
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .capacity = sizeof(ShaderRayCam),
      .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      .queue_type = BUFFER_QUEUE_GRAPHIC,
  };

  data->cam_buffer = rm_create_buffer(ctx->rm, &cam_info);

  data->cs_pipeline = pr_build_reg_cs(ctx->pr, config);
  data->cs_output_img = rm_create_image(ctx->rm, info);

  self->user_data = data;
}

static void _render(Sample *self, SampleContext *ctx) {
  RaytraceData *data = self->user_data;
  uint32_t groupsX = (ctx->extent.width + 7) / 8;
  uint32_t groupsY = (ctx->extent.height + 7) / 8;
  ResHandle swap_img = ctx->swap_img;

  world_cpu_tick(data->world, ctx->cam.pos);
  world_gpu_tick(data->world, ctx->cmd, ctx->rm, ctx->tq);

  float half_h = tanf((M_PI / 180) * ctx->cam.vfov_deg * 0.5f);
  float half_w = ctx->cam.aspect * half_h;

  ShaderRayCam gpu_cam = {.half_w_h = {half_w, half_h}};
  glmc_vec3_copy(ctx->cam.u, gpu_cam.u);
  glmc_vec3_copy(ctx->cam.v, gpu_cam.v);
  glmc_vec3_copy(ctx->cam.w, gpu_cam.w);
  gpu_cam.u[3] = ctx->cam.pos[0];
  gpu_cam.v[3] = ctx->cam.pos[1];
  gpu_cam.w[3] = ctx->cam.pos[2];

  cmd_buffer_upload(ctx->cmd, ctx->gpu, ctx->rm, data->cam_buffer, &gpu_cam, sizeof(gpu_cam));

  vec3 min_corner = {};
  world_grid_get_min_corner(data->world, min_corner);
  i32 grid_id = world_grid_get_push_id(data->world, ctx->rm);
  PushRay p = {
      .extent = {ctx->extent.width, ctx->extent.height},
      .cam_id = rm_get_buffer_index(ctx->rm, data->cam_buffer),
      .debug_mode = ctx->cam.debug_mode,
      .grid_id = grid_id,
  };
  glm_vec3_copy(min_corner, p.grid_min_corner);

  BindPipelineInfo b = {.p_push = &p, .push_size = sizeof(PushRay), .handle = data->cs_pipeline};

  cmd_bind_pipeline(ctx->cmd, ctx->pm, ctx->rm, &b);

  cmd_sync_image(ctx->cmd, ctx->rm, data->cs_output_img, STATE_SHADER, ACCESS_WRITE);
  cmd_sync_buffer(ctx->cmd, ctx->rm, data->cam_buffer, STATE_SHADER, ACCESS_READ);

  vkCmdDispatch(ctx->cmd.buffer, groupsX, groupsY, 1);
  cmd_image_copy_to_image(ctx->cmd, ctx->rm, data->cs_output_img, swap_img);
}

static void _resize(Sample *self, SampleContext *ctx) {
  RaytraceData *data = self->user_data;
  rm_resize_image(ctx->rm, data->cs_output_img, ctx->extent.width, ctx->extent.height);
}
