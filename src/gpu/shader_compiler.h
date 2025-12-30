#pragma once

#include "util.h"
#include <stdbool.h>
#include <volk.h>
// PUBLIC FUNCTIONS
typedef enum ShaderStage {
  SHADER_STAGE_VERTEX,
  SHADER_STAGE_FRAGMENT,
  SHADER_STAGE_COMPUTE
} ShaderStage;

typedef struct {
  VkShaderModule module;
  const char **headers;
  u32 count;
} CompileResult;

 CompileResult compile_glsl_to_spirv(VkDevice device, const char *path, ShaderStage stage);
