#ifndef RENDERGRAPH_H
#define RENDERGRAPH_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

// --- Defines & Enums ---
typedef enum {
  RG_ACCESS_NONE = 0,
  RG_ACCESS_READ = 1 << 0,
  RG_ACCESS_WRITE = 1 << 1,
  RG_ACCESS_READ_WRITE = RG_ACCESS_READ | RG_ACCESS_WRITE
} RGAccess;

typedef enum { RG_TASK_GRAPHIC, RG_TASK_COMPUTE, RG_TASK_TRANSFER } RGTaskType;

// --- Forward Declarations ---
typedef struct RenderGraph RenderGraph;
typedef void (*RGExecuteCallback)(VkCommandBuffer cmd, void *user_data);

// --- Resource Structures ---
typedef struct {
  uint32_t index;
  VkPipelineStageFlagBits2 stage;
  RGAccess access;
} RGBufferInfo;

typedef struct {
  uint32_t index;
  VkPipelineStageFlagBits2 stage;
  RGAccess access;
  VkImageLayout layout;
} RGImageInfo;

typedef struct {
  uint32_t index;
  VkAttachmentLoadOp loadOp;
  VkAttachmentStoreOp storeOp;
  VkClearValue clearValue;
} RGColorAttachmentInfo;

// --- Task Structure ---
typedef struct {
  const char *name;
  RGTaskType type;

  // Dynamic Arrays (Stretchy Buffers)
  RGBufferInfo *buffers;
  RGImageInfo *images;
  RGColorAttachmentInfo *colors;

  RGExecuteCallback execute;
  void *user_data;

  // Graph Logic
  uint32_t taskIndex;
  uint32_t *dependents;
  int32_t counter;
} RGTask;

// --- Internal Structures (Exposed for storage, opaque usage) ---
typedef struct {
  VkPipelineStageFlagBits2 lastStage;
  VkAccessFlags2 lastAccess;
  VkImageLayout lastLayout;
  VkPipelineStageFlagBits2 lastStageWrite;
  VkAccessFlags2 lastAccessWrite;
  VkImageLayout lastLayoutWrite;
  bool isFirst;
} RGResourceSyncInfo;

typedef struct {
  VkBuffer *buffers;
  VkImage *images;
  RGResourceSyncInfo *sync_infos;
  uint32_t resource_count;
  uint32_t capacity;
} RGResHandles;

// --- RenderGraph Context ---
struct RenderGraph {
  VkDevice device;
  RGTask *tasks; // Dynamic Array
  RGResHandles handles;
  struct RGSubgraph *subgraphs; // Dynamic Array defined in .c or fwd declared
};

// --- API ---
void rg_init(RenderGraph *rg, VkDevice device);
void rg_destroy(RenderGraph *rg);

void rg_import_buffer(RenderGraph *rg, uint32_t index, VkBuffer handle);
void rg_import_image(RenderGraph *rg, uint32_t index, VkImage handle,
                     VkImageLayout init_layout);

RGTask *rg_add_task(RenderGraph *rg, const char *name, RGTaskType type,
                    RGExecuteCallback exec, void *user_data);

// Task Building Helpers
void rg_task_read_buffer(RGTask *task, uint32_t res_idx,
                         VkPipelineStageFlagBits2 stage);
void rg_task_write_buffer(RGTask *task, uint32_t res_idx,
                          VkPipelineStageFlagBits2 stage);
void rg_task_read_image(RGTask *task, uint32_t res_idx,
                        VkPipelineStageFlagBits2 stage, VkImageLayout layout);
void rg_task_write_image(RGTask *task, uint32_t res_idx,
                         VkPipelineStageFlagBits2 stage, VkImageLayout layout);

void rg_compile(RenderGraph *rg);
void rg_execute(RenderGraph *rg, VkCommandBuffer cmd);

#endif