#pragma once

#include <stdbool.h>
#include <volk.h>

// Vi använder en enkel enum istället för att inkludera glslang-headers överallt
// PUBLIC FUNCTIONS
typedef enum ShaderStage {
  SHADER_STAGE_VERTEX,
  SHADER_STAGE_FRAGMENT,
  SHADER_STAGE_COMPUTE
} ShaderStage;

VkShaderModule compile_glsl_to_spirv(VkDevice device, const char *path,
                                     ShaderStage stage);

// Helper för att spara binärer om du vill cache-lagra SPIR-V i framtiden
bool file_write_binary(const char *path, const void *data, size_t size);
