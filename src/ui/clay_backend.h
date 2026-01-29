#pragma once

#include "command.h"
#include "common.h"
#include "gpu/gpu.h"
#include "gpu/pipeline.h"
#include "gpu/pipeline_hotreload.h"
#include "resource/resmanager.h"
#include <clay.h>

typedef struct ClayContext {
  // Clay State (Hidden from user)
  void *internal_memory;

  // Vulkan State
  M_GPU *gpu;
  M_Resource *rm;
  M_HotReload *pr;

  PipelineHandle pipeline;
  ResHandle vtx_buffer;
  ResHandle idx_buffer;
  ResHandle font_texture;

  uint32_t max_vertices;
  uint32_t max_indices;
} ClayContext;
// PUBLIC FUNCTIONS
void clay_backend_init(ClayContext *ctx, M_GPU *gpu, M_Resource *rm, M_HotReload *pr, CmdBuffer cmd, VkFormat format,
                       uint32_t width, uint32_t height);
void clay_backend_render(ClayContext *ctx, CmdBuffer cmd, M_Resource* rm, M_Swapchain * swap, uint32_t width,
                         uint32_t height);
void clay_backend_set_font_texture(ClayContext *ctx, ResHandle texture_handle);
void clay_backend_shutdown(ClayContext *ctx);
// END PUBLIC FUNCTIONS
