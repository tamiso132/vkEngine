#pragma once

#include "gpu/gpu.h"
#include "resmanager.h"
#include <stdbool.h>
#include <stdint.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

typedef uint32_t PipelineHandle;

typedef struct GpBuilder {
  // Shaders (Source paths for glsl)
  const char *name;

  const char *vs_path;
  const char *fs_path;

  // Dynamic Rendering
  VkFormat color_formats[4];
  uint32_t color_count;
  VkFormat depth_format;

  // State
  VkPrimitiveTopology topology;
  VkCullModeFlags cull_mode;
  VkFrontFace front_face;
  bool depth_test;
  bool depth_write;
  VkCompareOp depth_op;
  bool blend_enable;

  // Layout
  VkDescriptorSetLayout bindless_layout;
  uint32_t push_const_size;

  VkShaderModule vs;
  VkShaderModule fs;
} GpBuilder;

typedef struct {
  VkPipeline pipeline;
  VkPipelineLayout layout;
  GpBuilder config;
} GPUPipeline;

typedef struct PipelineManager M_Pipeline;
// PUBLIC FUNCTIONS

PipelineHandle gp_build_with_modules(M_Pipeline *pm, GpBuilder *b,
                                     VkShaderModule vs, VkShaderModule fs);

GPUPipeline *pm_get_pipeline(M_Pipeline *pm, PipelineHandle handle);

M_Pipeline *pm_init(ResourceManager *rm);

GPUDevice *pm_get_gpu(M_Pipeline *pm);

GpBuilder gp_init(ResourceManager *rm, const char* name);

void gp_set_shaders(GpBuilder *b, VkShaderModule vs, VkShaderModule fs);
void gp_set_topology(GpBuilder *b, VkPrimitiveTopology topo);
void gp_set_cull(GpBuilder *b, VkCullModeFlags mode, VkFrontFace front);
void gp_set_depth_format(GpBuilder *b, VkFormat format);
void gp_enable_depth(GpBuilder *b, bool write, VkCompareOp op);
void gp_enable_blend(GpBuilder *b);

void gp_set_color_formats(GpBuilder *b, const VkFormat *formats,
                          uint32_t count);

void gp_set_layout(GpBuilder *b, VkDescriptorSetLayout bindless,
                   uint32_t push_size);

PipelineHandle gp_build(M_Pipeline *pm, GpBuilder *b);

void gp_destroy(VkDevice device, GPUPipeline *p);
