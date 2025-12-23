#ifndef RENDERGRAPH_H
#define RENDERGRAPH_H

#include "resmanager.h"

// --- Enums ---
typedef enum
{
    RG_USAGE_VERTEX,
    RG_USAGE_INDEX,
    RG_USAGE_UNIFORM,
    RG_USAGE_SAMPLED,
    RG_USAGE_STORAGE_READ,
    RG_USAGE_STORAGE_WRITE,
    RG_USAGE_TRANSFER_SRC,
    RG_USAGE_TRANSFER_DST,
    RG_USAGE_COLOR,
    RG_USAGE_DEPTH
} RGUsage;

typedef enum
{
    RG_TASK_GRAPHIC,
    RG_TASK_COMPUTE,
    RG_TASK_TRANSFER
} RGTaskType;

// --- Types ---
typedef struct RenderGraph RenderGraph;
typedef void (*RGExecuteCallback)(VkCommandBuffer cmd, void *user_data);

// The "Pass Handle" - Acts as your builder
typedef struct
{
    RenderGraph *rg;
    uint32_t id;
} RGPass;

// --- Main API ---

RenderGraph *rg_init(ResourceManager *rm);
void rg_destroy(RenderGraph *rg);

// 1. Create a Pass (Returns the handle/builder)
RGPass rg_add_pass(RenderGraph *rg, const char *name, RGTaskType type,
                   RGExecuteCallback exec, void *user_data);

void rg_compile(RenderGraph *rg);
void rg_execute(RenderGraph *rg, VkCommandBuffer cmd);

// --- Pass Configuration Functions ---
// These are the "possible things you can add" to a pass.

// Basic Resource Dependency
void rg_pass_read(RGPass pass, RGHandle res, RGUsage usage);
void rg_pass_write(RGPass pass, RGHandle res, RGUsage usage);

// Graphics Pipeline Specifics (Render Targets)
// Note: These are shortcuts for "Write + LoadOp/StoreOp setup"
void rg_pass_set_color_target(RGPass pass, RGHandle res, VkClearValue clear_color);
void rg_pass_set_depth_target(RGPass pass, RGHandle res, VkClearValue clear_depth);

// (Future expansion: rg_pass_set_viewport, rg_pass_set_push_constants, etc.)

#endif // RENDERGRAPH_H