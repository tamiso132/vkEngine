#pragma once

#include "gpu/gpu.h"
#include "resmanager.h"
#include <stdbool.h>
#include <stdint.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>

typedef enum PipelineType {
  PIPELINE_TYPE_COMPUTE,
  PIPELINE_TYPE_GRAPHIC,
} PipelineType;

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
} GpConfig;

typedef struct CpConfig {
  const char *cs_path;
  VkShaderModule module;
} CpConfig;

typedef struct {
  VkPipeline vk_handle;
  PipelineType type;
  union {
    GpConfig gp_config;
    CpConfig cp_config;
  };
} GPUPipeline;

// PUBLIC FUNCTIONS

M_Pipeline *pm_init(ResourceManager *rm);
GPUDevice *pm_get_gpu(M_Pipeline *pm);
ResourceManager *pm_get_rm(M_Pipeline *pm);
GPUPipeline *pm_get_pipeline(M_Pipeline *pm, PipelineHandle handle);

// P COMPUTE BUILDER
CpConfig cp_init(const char *name);
void cp_set_shader(CpConfig *config, VkShaderModule module);
void cp_set_shader_path(CpConfig *config, const char *path);
PipelineHandle cp_build(M_Pipeline *pm, CpConfig *config);
void cp_rebuild(M_Pipeline *pm, CpConfig *config, PipelineHandle handle);

// P GRAPHIC BUILDER
GpConfig gp_init(ResourceManager *rm, const char *name);
void gp_set_shaders(GpConfig *b, VkShaderModule vs, VkShaderModule fs);
void gp_set_topology(GpConfig *b, VkPrimitiveTopology topo);
void gp_set_cull(GpConfig *b, VkCullModeFlags mode, VkFrontFace front);
void gp_set_depth_format(GpConfig *b, VkFormat format);
void gp_enable_depth(GpConfig *b, bool write, VkCompareOp op);
void gp_enable_blend(GpConfig *b);
void gp_set_color_formats(GpConfig *b, const VkFormat *formats, uint32_t count);
void gp_set_layout(GpConfig *b, VkDescriptorSetLayout bindless, uint32_t push_size);
void gp_rebuild(M_Pipeline *pm, GpConfig *b, PipelineHandle handle);
PipelineHandle gp_build(M_Pipeline *pm, GpConfig *b);

void gp_destroy(VkDevice device, GPUPipeline *p);
