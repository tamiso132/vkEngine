#include "filewatch.h"
#include "pipeline.h"
#include "util.h"
#include <stdlib.h>

typedef struct {
  VkDevice device;
  GPUPipeline *target;
  GpBuilder config;
} ReloadCtx;

static void on_shader_file_changed(const char *path, void *user_data) {
  ReloadCtx *ctx = (ReloadCtx *)user_data;
  vkDeviceWaitIdle(ctx->device);

  GPUPipeline new_pipe = gp_build(ctx->device, &ctx->config);
  if (new_pipe.pipeline != VK_NULL_HANDLE) {
    gp_destroy(ctx->device, ctx->target);
    *ctx->target = new_pipe;
    LOG_INFO("[HotReload] Shader %s updated.\n", path);
  }
}

void gp_register_hotreload(VkDevice device, GPUPipeline *target, GpBuilder *b) {
  // if (!g_filewatcher)
  //   return;
  //
  // ReloadCtx *ctx = malloc(sizeof(ReloadCtx));
  // ctx->device = device;
  // ctx->target = target;
  // ctx->config = *b;
  //
  // if (b->vs_path)
  //   fw_add_watch(g_filewatcher, b->vs_path, on_shader_file_changed, ctx);
  // if (b->fs_path)
  //   fw_add_watch(g_filewatcher, b->fs_path, on_shader_file_changed, ctx);
}
