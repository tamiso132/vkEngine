#ifndef RENDERGRAPH_H
#define RENDERGRAPH_H

#include "resmanager.h"
#include <stdbool.h>

// --- Types ---

typedef enum { RG_TASK_GRAPHIC, RG_TASK_COMPUTE, RG_TASK_TRANSFER } RGTaskType;

typedef enum {
  RG_USAGE_READ = 1 << 0,
  RG_USAGE_WRITE = 1 << 1,
  RG_USAGE_READ_WRITE = RG_USAGE_READ | RG_USAGE_WRITE
} RGUsageType;

// Defines how a resource is used in a specific pass
typedef struct {
  ResHandle handle;
  RGUsageType type;
  VkPipelineStageFlags2 stage;
  VkAccessFlags2 access;
  VkImageLayout layout; // Only relevant for images
} RGResourceUsage;

// User callback function pointer
typedef void (*RGPassFunc)(VkCommandBuffer cmd, void *user_data);

typedef struct {
  char name[64];
  RGTaskType type;
  RGPassFunc func;
  void *user_data;

  // Resources used by this pass
  RGResourceUsage *usages;
  uint32_t usage_count;
  uint32_t usage_cap;

  // Barriers to execute BEFORE this pass runs (Calculated during compile)
  VkImageMemoryBarrier2 *img_barriers;
  uint32_t img_barrier_count;

  VkBufferMemoryBarrier2 *buf_barriers;
  uint32_t buf_barrier_count;
} RGPass;

// Tracks the current state of a resource (Replaces C++ ResourceSyncInfo)
// Used internally during compilation to calculate transitions.
typedef struct {
  VkPipelineStageFlags2 last_stage;
  VkAccessFlags2 last_access;
  VkImageLayout last_layout; // Images only
  bool is_written;           // Was the last access a write?
} RGSyncState;

typedef struct {
  ResourceManager *rm;

  RGPass *passes;
  uint32_t pass_count;
  uint32_t pass_cap;

  // Internal state tracking for compilation
  // This array is sized to match RM_MAX_RESOURCES
  RGSyncState *resource_states;
} RenderGraph;

// --- API ---

// Lifecycle
RenderGraph *rg_init(ResourceManager *rm);
void rg_destroy(RenderGraph *rg);

// 1. Define Graph
// Adds a pass node to the graph. Returns a pointer to the pass so you can add
// usages to it.
RGPass *rg_add_pass(RenderGraph *rg, const char *name, RGTaskType type,
                    RGPassFunc func, void *user_data);

// 2. Define Usage (Builder pattern)
// Declares that 'pass' reads 'resource' at 'stage'.
void rg_pass_read(RGPass *pass, ResHandle resource,
                  VkPipelineStageFlags2 stage);

// Declares that 'pass' writes to 'resource' at 'stage'.
void rg_pass_write(RGPass *pass, ResHandle resource,
                   VkPipelineStageFlags2 stage);

// Helper: Declares a Color Attachment Write (Output)
// Implies RG_USAGE_WRITE | COLOR_ATTACHMENT_OUTPUT | COLOR_ATTACHMENT_OPTIMAL
void rg_pass_set_color_target(RGPass *pass, ResHandle image,
                              VkClearValue clear);

// Helper: Declares a Texture Read (Sampled)
// Implies RG_USAGE_READ | FRAGMENT_SHADER | SHADER_READ_ONLY_OPTIMAL
void rg_pass_texture_read(RGPass *pass, ResHandle image);

// 3. Compile & Execute
// Analyses the passes and injects necessary barriers into them.
void rg_compile(RenderGraph *rg);

// Submits the barriers and executes the pass callbacks into the command buffer.
void rg_execute(RenderGraph *rg, VkCommandBuffer cmd);

#endif // RENDERGRAPH_H
