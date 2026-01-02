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
  VECTOR_TYPES(ReloadCtx) Vector reg_pips;

} M_HotReload;

// --- Private Prototypes ---
static bool _update_modifed();
static bool _system_init(void *config, u32 *mem_req);
static void _init(M_HotReload *pr, M_Pipeline *pm);

SystemFunc pr_system_get_func() {
  return (SystemFunc){
      .on_init = _system_init,
      .on_update = _update_modifed,
  };
}

// TODO, remove vkPipeline, and only use handles
PipelineHandle pr_build_reg_cs(M_HotReload *pr, CpConfig b) {
  if (!b.cs_path) {
    LOG_ERROR("[SHADER_COMPILATION] Compute Config has a Null path");
    abort();
  }
  auto *fm = SYSTEM_GET(SYSTEM_TYPE_FILE, M_File);
  auto *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);

  FileGroup *fg = fg_init(fm);

  CompileResult cs_result = {.shader_path = b.cs_path, .include_dir = str_get_dir(b.cs_path), .fg = fg};

  if (shader_compile_glsl(dev->device, &cs_result, SHADER_STAGE_COMPUTE) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s,]", b.cs_path);
    abort();
  }

  cp_set_shader(&b, cs_result.module);

  PipelineHandle pip_handle = cp_build(pm, &b);
  ReloadCtx rel = {.fg = fg, .handle = pip_handle};
  vec_push(&pr->reg_pips, &rel);

  return pip_handle;
}

PipelineHandle pr_build_reg(M_HotReload *pr, GpConfig *b, const char *vs_path, const char *fs_path) {
  auto *fm = SYSTEM_GET(SYSTEM_TYPE_FILE, M_File);
  auto *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);

  FileGroup *fg = fg_init(fm);

  CompileResult vs_result = {.shader_path = vs_path, .include_dir = str_get_dir(vs_path), .fg = fg};
  CompileResult fs_result = {.shader_path = fs_path, .include_dir = str_get_dir(fs_path), .fg = fg};

  if (shader_compile_glsl(dev->device, &vs_result, SHADER_STAGE_VERTEX) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s,]", vs_path);
    abort();
  }

  if (shader_compile_glsl(dev->device, &fs_result, SHADER_STAGE_FRAGMENT) != SHADER_SUCCESS) {
    LOG_ERROR("[SHADER_COMPILATION] Failed to compile: [%s]", fs_path);
    abort();
  }

  gp_set_shaders(b, vs_result.module, fs_result.module);

  PipelineHandle pip_handle = gp_build(pm, b);
  ReloadCtx rel = {.fg = fg, .handle = pip_handle};
  vec_push(&pr->reg_pips, &rel);

  return pip_handle;
}

// --- Private Functions ---

// TODO, remove vkPipeline, and only use handles
static bool _update_modifed() {

  auto *device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  auto *pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);

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
  return true;
}

static bool _system_init(void *config, u32 *mem_req) {
  SYSTEM_HELPER_MEM(mem_req, M_HotReload);
  auto *pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  _init(pr, pm);
  return true;
}

static void _init(M_HotReload *pr, M_Pipeline *pm) { vec_init(&pr->reg_pips, sizeof(ReloadCtx), NULL); }
