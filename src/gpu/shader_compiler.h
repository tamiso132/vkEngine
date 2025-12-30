#pragma once

#include "filewatch.h"
#include "util.h"
#include <stdbool.h>
#include <volk.h>

typedef enum ShaderStage {
  SHADER_STAGE_VERTEX,
  SHADER_STAGE_FRAGMENT,
  SHADER_STAGE_COMPUTE
} ShaderStage;

typedef struct {
  // INFO
  FileGroup *fg;
  const char *shader_path;
  const char *include_dir;

  // Results
  VkShaderModule module;

  // internals
  Vector _temp;
} CompileResult;

// PUBLIC FUNCTIONS

void shader_compile_glsl(VkDevice device, CompileResult *result,
                         ShaderStage stage);
