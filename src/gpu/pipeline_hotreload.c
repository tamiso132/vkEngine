#include "common.h"
#include "filewatch.h"
#include "gpu/gpu.h"
#include "gpu/shader_compiler.h"
#include "pipeline.h"
#include "system_manager.h"
#include "util.h"
#include "vector.h"
typedef struct {
  FileGroup *fg;
  PipelineHandle handle;
} ReloadCtx;

typedef struct M_HotReload {
  FileGroup *fm;
  VECTOR_TYPES(ReloadCtx) Vector reg_pips;

} M_HotReload;

// --- Private Prototypes ---

M_HotReload *pr_init(M_Pipeline *pm) {
  M_HotReload *m_reloader = calloc(sizeof(M_HotReload), 1);
  vec_init(&m_reloader->reg_pips, sizeof(ReloadCtx), NULL);
  return m_reloader;
}
// TODO, remove vkPipeline, and only use handles
void pr_update_modifed(M_HotReload *pr) {
  auto *device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  for (u32 i = 0; i < vec_len(&pr->reg_pips); i++) {
    ReloadCtx *ctx = VEC_AT(&pr->reg_pips, i, ReloadCtx);

    // Shader files are modified
    if (fg_is_modified(ctx->fg)) {
      GPUPipeline *pipeline = pm_get_pipeline(pm, ctx->handle);
      if (pipeline->type == PIPELINE_TYPE_GRAPHIC) {
        CompileResult vs_result = {};
        vs_result.shader_path = pipeline->gp_config.vs_path;
        vs_result.include_dir = str_get_dir(pipeline->gp_config.vs_path);
        vs_result.fg = ctx->fg;

        if (shader_compile_glsl(device->device, &vs_result, SHADER_STAGE_VERTEX) != SHADER_SUCCESS) {
          continue;
        }

        CompileResult fs_result = {};
        fs_result.shader_path = pipeline->gp_config.fs_path;
        fs_result.include_dir = str_get_dir(pipeline->gp_config.fs_path);
        fs_result.fg = ctx->fg;

        if (shader_compile_glsl(device->device, &fs_result, SHADER_STAGE_FRAGMENT) != SHADER_SUCCESS) {
          continue;
        }

        gp_set_shaders(&pipeline->gp_config, vs_result.module, fs_result.module);
        gp_rebuild(&pipeline->gp_config, ctx->handle);
      } else {
        CompileResult cs_result = {};
        cs_result.shader_path = pipeline->cp_config.cs_path;
        cs_result.include_dir = str_get_dir(pipeline->cp_config.cs_path);
        cs_result.fg = ctx->fg;

        if (shader_compile_glsl(device->device, &cs_result, SHADER_STAGE_COMPUTE) != SHADER_SUCCESS) {
          continue;
        }

        cp_set_shader(&pipeline->cp_config, cs_result.module);
        cp_rebuild(&pipeline->cp_config, ctx->handle);
      }
    }
  }
}

// TODO, remove vkPipeline, and only use handles
PipelineHandle pr_build_reg_cs(M_HotReload *pr, CpConfig b) {
  if (!b.cs_path) {
    LOG_ERROR("[SHADER_COMPILATION] Compute Config has a Null path");
    abort();
  }

  FileGroup *fg = fg_init(pr);

  auto device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  CompileResult cs_result = {.shader_path = b.cs_path, .include_dir = str_get_dir(b.cs_path), .fg = fg};

  if (shader_compile_glsl(device, &cs_result, SHADER_STAGE_COMPUTE) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s,]", b.cs_path);
    abort();
  }

  cp_set_shader(&b, cs_result.module);

  PipelineHandle pip_handle = cp_build(pr->pm, &b);
  ReloadCtx rel = {.fg = fg, .handle = pip_handle};
  vec_push(&pr->reg_pips, &rel);

  return pip_handle;
}
PipelineHandle pr_build_reg(M_HotReload *pr, GpConfig *b, const char *vs_path, const char *fs_path) {
  FileGroup *fg = fg_init(pr->fm);

  auto device = pm_get_gpu(pr->pm)->device;

  CompileResult vs_result = {.shader_path = vs_path, .include_dir = str_get_dir(vs_path), .fg = fg};
  CompileResult fs_result = {.shader_path = fs_path, .include_dir = str_get_dir(fs_path), .fg = fg};

  if (shader_compile_glsl(device, &vs_result, SHADER_STAGE_VERTEX) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s,]", vs_path);
    abort();
  }

  if (shader_compile_glsl(device, &fs_result, SHADER_STAGE_FRAGMENT) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s]", fs_path);
    abort();
  }

  gp_set_shaders(b, vs_result.module, fs_result.module);

  PipelineHandle pip_handle = gp_build(pr->pm, b);
  ReloadCtx rel = {.fg = fg, .handle = pip_handle};
  vec_push(&pr->reg_pips, &rel);

  return pip_handle;
}
// --- Private Functions ---
