#include "filewatch.h"
#include "gpu/gpu.h"
#include "gpu/shader_compiler.h"
#include "pipeline.h"
#include "util.h"
#include "vector.h"

typedef struct {
  FileGroup *fg;
  PipelineHandle handle;
} ReloadCtx;

typedef struct PipelineReloader {
  M_Pipeline *pm;
  FileManager *fm;

  VECTOR_TYPES(ReloadCtx)
  Vector reg_pips;

} M_PipelineReloader;

// --- Private Prototypes ---

M_PipelineReloader *pr_init(M_Pipeline *pm, FileManager *fm) {
  M_PipelineReloader *m_reloader = calloc(sizeof(M_PipelineReloader), 1);
  m_reloader->fm = fm;
  m_reloader->pm = pm;
  vec_init(&m_reloader->reg_pips, sizeof(ReloadCtx), NULL);
  return m_reloader;
}

void pr_update_modifed(M_PipelineReloader *pr) {
  GPUDevice *device = pm_get_gpu(pr->pm);
  for (u32 i = 0; i < vec_len(&pr->reg_pips); i++) {
    ReloadCtx *ctx = VEC_AT(&pr->reg_pips, i, ReloadCtx);

    // Shader files are modified
    if (fg_is_modified(ctx->fg)) {
      GPUPipeline *pipeline = pm_get_pipeline(pr->pm, ctx->handle);

      CompileResult vs_result = {};
      vs_result.shader_path = pipeline->config.vs_path;
      vs_result.include_dir = str_get_dir(pipeline->config.vs_path);
      vs_result.fg = ctx->fg;

      shader_compile_glsl(device->device, &vs_result, SHADER_STAGE_VERTEX);

      CompileResult fs_result = {};
      fs_result.shader_path = pipeline->config.fs_path;
      fs_result.include_dir = str_get_dir(pipeline->config.fs_path);
      fs_result.fg = ctx->fg;

      shader_compile_glsl(device->device, &fs_result, SHADER_STAGE_FRAGMENT);
      gp_set_shaders(&pipeline->config, vs_result.module, fs_result.module);
      gp_rebuild(pr->pm, &pipeline->config, ctx->handle);
    }
  }
}

PipelineHandle pr_build_reg(M_PipelineReloader *pr, GpBuilder *b, const char *vs_path, const char *fs_path) {
  FileGroup *fg = fg_init(pr->fm);

  auto device = pm_get_gpu(pr->pm)->device;

  CompileResult vs_result = {.shader_path = vs_path, .include_dir = str_get_dir(vs_path), .fg = fg};

  CompileResult fs_result = {.shader_path = fs_path, .include_dir = str_get_dir(fs_path), .fg = fg};

  shader_compile_glsl(device, &vs_result, SHADER_STAGE_VERTEX);
  shader_compile_glsl(device, &fs_result, SHADER_STAGE_FRAGMENT);

  gp_set_shaders(b, vs_result.module, fs_result.module);

  PipelineHandle pip_handle = gp_build(pr->pm, b);
  ReloadCtx rel = {.fg = fg, .handle = pip_handle};
  vec_push(&pr->reg_pips, &rel);

  return pip_handle;
}
// --- Private Functions ---
