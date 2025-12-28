#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <volk.h>

// --- Types ---

typedef struct {
  VkPipeline pipeline;
  VkPipelineLayout layout;
} GPUPipeline;

// The Builder Struct (Holds state before creation)
typedef struct {
  // Shaders (Source paths for runtime compilation)
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
} GpBuilder;

// --- API ---

// 1. Start (Returns builder with valid defaults: No depth, Triangle List, No
// blend)

// 2. Configure (Chainable-ish helpers)

// Rendering Setup
void gp_set_color_formats(GpBuilder *b, const VkFormat *formats,
                          uint32_t count);

// Depth & Blend

// Layout
void gp_set_layout(GpBuilder *b, VkDescriptorSetLayout bindless,
                   uint32_t push_size);

// 3. Build (Compiles shaders and creates pipeline)
// Note: Returns the pipeline by value.

// 4. Hot Reloading
// Register a stable pointer to a pipeline struct so it can be updated
// automatically when the shader files change.

// Cleanup

#endif // PIPELINE_H

// PUBLIC FUNCTIONS
bool write_file_binary(const char *path, const void *data, size_t size);

// 1. Start (Returns builder with valid defaults: No depth, Triangle List, No
// blend)
GpBuilder gp_init();

// 2. Configure (Chainable-ish helpers)
void gp_set_shaders(GpBuilder *b, const char *vs, const char *fs);
void gp_set_topology(GpBuilder *b, VkPrimitiveTopology topo);
void gp_set_cull(GpBuilder *b, VkCullModeFlags mode, VkFrontFace front);
void gp_set_depth_format(GpBuilder *b, VkFormat format);

// Depth & Blend
void gp_enable_depth(GpBuilder *b, bool write, VkCompareOp op);
void gp_enable_blend(GpBuilder *b);

// 3. Build (Compiles shaders and creates pipeline)
// Note: Returns the pipeline by value.
GPUPipeline gp_build(VkDevice device, GpBuilder *b);

// 4. Hot Reloading
// Register a stable pointer to a pipeline struct so it can be updated
// automatically when the shader files change.
void gp_register_hotreload(VkDevice device, GPUPipeline *target, GpBuilder *b);

// Cleanup
void gp_destroy(VkDevice device, GPUPipeline *p);
