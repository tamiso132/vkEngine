#include "rendergraph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==========================================
// 1. Minimal Stretchy Buffer (Vector)
// ==========================================
typedef struct {
  size_t len;
  size_t cap;
} VecHdr;
#define v_hdr(v) ((VecHdr *)((char *)(v) - sizeof(VecHdr)))
#define v_len(v) ((v) ? v_hdr(v)->len : 0)
#define v_cap(v) ((v) ? v_hdr(v)->cap : 0)
#define v_push(v, val) ((v) = _v_grow(v, sizeof(*(v))), (v)[v_hdr(v)->len++] = (val))
#define v_free(v) ((v) ? (free(v_hdr(v)), (v) = NULL) : 0)

static void *
_v_grow(void *v, size_t elem_size)
{
  size_t len = v_len(v);
  size_t cap = v_cap(v);
  if(len >= cap) {
    size_t  new_cap  = cap ? cap * 2 : 8;
    size_t  new_size = sizeof(VecHdr) + new_cap * elem_size;
    VecHdr *new_hdr  = (VecHdr *)realloc(v ? v_hdr(v) : NULL, new_size);
    new_hdr->len     = len;
    new_hdr->cap     = new_cap;
    return (void *)((char *)new_hdr + sizeof(VecHdr));
  }
  return v;
}

// ==========================================
// 2. Internal Structs (Flattened)
// ==========================================

// A batch of work for a single task inside a subgraph
typedef struct {
  uint32_t                taskId;
  VkImageMemoryBarrier2  *imageBarriers;  // vector
  VkBufferMemoryBarrier2 *bufferBarriers; // vector
} RGTaskBatch;

// A Subgraph (Chain of batches)
typedef struct RGSubgraph {
  RGTaskBatch *tasks;      // vector
  uint32_t    *dependents; // vector
} RGSubgraph;

// ==========================================
// 3. Helpers
// ==========================================
static VkAccessFlags2
get_vk_access_flags(VkPipelineStageFlagBits2 stage, RGAccess access)
{
  VkAccessFlags2 flags = 0;
  if(access & RG_ACCESS_WRITE) {
    if(stage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)
      flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    else if(stage == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
      flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
    else if(stage == VK_PIPELINE_STAGE_2_TRANSFER_BIT)
      flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
  }
  if(access & RG_ACCESS_READ) {
    if(stage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT)
      flags |= VK_ACCESS_2_SHADER_READ_BIT;
    else if(stage == VK_PIPELINE_STAGE_2_TRANSFER_BIT)
      flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
    else if(stage == VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)
      flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  }
  return flags;
}

// ==========================================
// 4. API Implementation
// ==========================================

void
rg_init(RenderGraph *rg, VkDevice device)
{
  memset(rg, 0, sizeof(RenderGraph));
  rg->device = device;

  // Fixed capacity for simplicity, real code should resize
  rg->handles.capacity       = 256;
  rg->handles.buffers        = calloc(256, sizeof(VkBuffer));
  rg->handles.images         = calloc(256, sizeof(VkImage));
  rg->handles.sync_infos     = calloc(256, sizeof(RGResourceSyncInfo));
  rg->handles.resource_count = 0;
}

void
rg_destroy(RenderGraph *rg)
{
  for(size_t i = 0; i < v_len(rg->tasks); i++) {
    v_free(rg->tasks[i].buffers);
    v_free(rg->tasks[i].images);
    v_free(rg->tasks[i].colors);
    v_free(rg->tasks[i].dependents);
  }
  v_free(rg->tasks);

  for(size_t i = 0; i < v_len(rg->subgraphs); i++) {
    for(size_t j = 0; j < v_len(rg->subgraphs[i].tasks); j++) {
      v_free(rg->subgraphs[i].tasks[j].imageBarriers);
      v_free(rg->subgraphs[i].tasks[j].bufferBarriers);
    }
    v_free(rg->subgraphs[i].tasks);
    v_free(rg->subgraphs[i].dependents);
  }
  v_free(rg->subgraphs);

  free(rg->handles.buffers);
  free(rg->handles.images);
  free(rg->handles.sync_infos);
}

void
rg_import_buffer(RenderGraph *rg, uint32_t index, VkBuffer handle)
{
  if(index >= rg->handles.capacity)
    return;
  rg->handles.buffers[index] = handle;

  RGResourceSyncInfo *info = &rg->handles.sync_infos[index];
  memset(info, 0, sizeof(RGResourceSyncInfo));
  info->isFirst        = true;
  info->lastStageWrite = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
}

void
rg_import_image(RenderGraph *rg, uint32_t index, VkImage handle, VkImageLayout init_layout)
{
  if(index >= rg->handles.capacity)
    return;
  rg->handles.images[index] = handle;

  RGResourceSyncInfo *info = &rg->handles.sync_infos[index];
  memset(info, 0, sizeof(RGResourceSyncInfo));
  info->isFirst         = true;
  info->lastLayout      = init_layout;
  info->lastLayoutWrite = init_layout;
  info->lastStageWrite  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
}

RGTask *
rg_add_task(RenderGraph *rg, const char *name, RGTaskType type, RGExecuteCallback exec, void *user_data)
{
  RGTask t    = { 0 };
  t.name      = name;
  t.type      = type;
  t.execute   = exec;
  t.user_data = user_data;
  t.taskIndex = (uint32_t)v_len(rg->tasks);
  v_push(rg->tasks, t);
  return &rg->tasks[v_len(rg->tasks) - 1];
}

void
rg_task_read_buffer(RGTask *task, uint32_t idx, VkPipelineStageFlagBits2 stage)
{
  RGBufferInfo info = { .index = idx, .stage = stage, .access = RG_ACCESS_READ };
  v_push(task->buffers, info);
}

void
rg_task_write_buffer(RGTask *task, uint32_t idx, VkPipelineStageFlagBits2 stage)
{
  RGBufferInfo info = { .index = idx, .stage = stage, .access = RG_ACCESS_WRITE };
  v_push(task->buffers, info);
}

void
rg_task_read_image(RGTask *task, uint32_t idx, VkPipelineStageFlagBits2 stage, VkImageLayout layout)
{
  RGImageInfo info = { .index = idx, .stage = stage, .access = RG_ACCESS_READ, .layout = layout };
  v_push(task->images, info);
}

void
rg_task_write_image(RGTask *task, uint32_t idx, VkPipelineStageFlagBits2 stage, VkImageLayout layout)
{
  RGImageInfo info = { .index = idx, .stage = stage, .access = RG_ACCESS_WRITE, .layout = layout };
  v_push(task->images, info);
}

// ==========================================
// 5. Compile Logic
// ==========================================

void
rg_compile(RenderGraph *rg)
{
  // A. Reset Logic
  for(size_t i = 0; i < v_len(rg->tasks); ++i) {
    rg->tasks[i].counter = 0;
    v_free(rg->tasks[i].dependents);
  }

  // B. Build Dependency Graph
  uint32_t **nextReader = calloc(rg->handles.capacity, sizeof(uint32_t *));
  uint32_t **nextWriter = calloc(rg->handles.capacity, sizeof(uint32_t *));

  for(int i = (int)v_len(rg->tasks) - 1; i >= 0; --i) {
    RGTask *node = &rg->tasks[i];

    // Process Buffers
    for(size_t b = 0; b < v_len(node->buffers); b++) {
      uint32_t rid = node->buffers[b].index;
      if(node->buffers[b].access & RG_ACCESS_WRITE) {
        // Anyone reading/writing after me depends on me
        if(nextReader[rid]) {
          for(size_t k = 0; k < v_len(nextReader[rid]); k++) {
            v_push(node->dependents, nextReader[rid][k]);
            rg->tasks[nextReader[rid][k]].counter++;
          }
        }
        if(nextWriter[rid]) {
          for(size_t k = 0; k < v_len(nextWriter[rid]); k++) {
            v_push(node->dependents, nextWriter[rid][k]);
            rg->tasks[nextWriter[rid][k]].counter++;
          }
        }
        v_push(nextWriter[rid], (uint32_t)i);
      } else { // READ
        // Only writers after me depend on me
        if(nextWriter[rid]) {
          for(size_t k = 0; k < v_len(nextWriter[rid]); k++) {
            v_push(node->dependents, nextWriter[rid][k]);
            rg->tasks[nextWriter[rid][k]].counter++;
          }
        }
        v_push(nextReader[rid], (uint32_t)i);
      }
    }

    // Process Images (Same logic)
    for(size_t m = 0; m < v_len(node->images); m++) {
      uint32_t rid = node->images[m].index;
      if(node->images[m].access & RG_ACCESS_WRITE) {
        if(nextReader[rid])
          for(size_t k = 0; k < v_len(nextReader[rid]); k++) {
            v_push(node->dependents, nextReader[rid][k]);
            rg->tasks[nextReader[rid][k]].counter++;
          }
        if(nextWriter[rid])
          for(size_t k = 0; k < v_len(nextWriter[rid]); k++) {
            v_push(node->dependents, nextWriter[rid][k]);
            rg->tasks[nextWriter[rid][k]].counter++;
          }
        v_push(nextWriter[rid], (uint32_t)i);
      } else {
        if(nextWriter[rid])
          for(size_t k = 0; k < v_len(nextWriter[rid]); k++) {
            v_push(node->dependents, nextWriter[rid][k]);
            rg->tasks[nextWriter[rid][k]].counter++;
          }
        v_push(nextReader[rid], (uint32_t)i);
      }
    }
  }

  // Cleanup Maps
  for(uint32_t k = 0; k < rg->handles.capacity; k++) {
    v_free(nextReader[k]);
    v_free(nextWriter[k]);
  }
  free(nextReader);
  free(nextWriter);

  // C. Flatten into Subgraphs
  for(uint32_t i = 0; i < v_len(rg->tasks); i++) {
    if(rg->tasks[i].counter == 0) {
      RGSubgraph sg      = { 0 };
      uint32_t   currIdx = i;

      while(true) {
        RGTask *task  = &rg->tasks[currIdx];
        task->counter = -1; // Mark visited

        VkBufferMemoryBarrier2 *bufBars = NULL;
        VkImageMemoryBarrier2  *imgBars = NULL;

        // 1. Generate Buffer Barriers
        for(size_t b = 0; b < v_len(task->buffers); b++) {
          RGBufferInfo       *info      = &task->buffers[b];
          RGResourceSyncInfo *sync      = &rg->handles.sync_infos[info->index];
          VkAccessFlags2      dstAccess = get_vk_access_flags(info->stage, info->access);

          if(!sync->isFirst) {
            VkBufferMemoryBarrier2 bar
                = { .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .srcStageMask  = sync->lastStageWrite, // Simplified: Real logic needs to check Read vs Write last
                    .srcAccessMask = sync->lastAccessWrite,
                    .dstStageMask  = info->stage,
                    .dstAccessMask = dstAccess,
                    .buffer        = rg->handles.buffers[info->index],
                    .size          = VK_WHOLE_SIZE };
            v_push(bufBars, bar);
          }
          if(info->access & RG_ACCESS_WRITE) {
            sync->lastStageWrite  = info->stage;
            sync->lastAccessWrite = dstAccess;
            sync->isFirst         = false;
          }
        }

        // 2. Generate Image Barriers
        for(size_t m = 0; m < v_len(task->images); m++) {
          RGImageInfo        *info      = &task->images[m];
          RGResourceSyncInfo *sync      = &rg->handles.sync_infos[info->index];
          VkAccessFlags2      dstAccess = get_vk_access_flags(info->stage, info->access);

          if(sync->lastLayout != info->layout || (info->access & RG_ACCESS_WRITE)) {
            VkImageMemoryBarrier2 bar = { .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                          .image            = rg->handles.images[info->index],
                                          .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
                                          .srcStageMask     = sync->lastStage,
                                          .srcAccessMask    = sync->lastAccess,
                                          .oldLayout        = sync->lastLayout,
                                          .dstStageMask     = info->stage,
                                          .dstAccessMask    = dstAccess,
                                          .newLayout        = info->layout };
            v_push(imgBars, bar);
          }
          sync->lastLayout = info->layout;
          sync->lastStage  = info->stage;
          sync->lastAccess = dstAccess;
          if(info->access & RG_ACCESS_WRITE) {
            sync->lastStageWrite  = info->stage;
            sync->lastAccessWrite = dstAccess;
            sync->lastLayoutWrite = info->layout;
          }
          sync->isFirst = false;
        }

        // 3. Add Batch
        RGTaskBatch batch = { .taskId = currIdx, .bufferBarriers = bufBars, .imageBarriers = imgBars };
        v_push(sg.tasks, batch);

        // 4. Continue Chain?
        if(v_len(task->dependents) == 0)
          break;
        if(v_len(task->dependents) > 1) {
          for(size_t d = 0; d < v_len(task->dependents); d++)
            rg->tasks[task->dependents[d]].counter--;
          break;
        }
        uint32_t nextId = task->dependents[0];
        if(rg->tasks[nextId].counter != 1) {
          rg->tasks[nextId].counter--;
          break;
        }
        currIdx = nextId;
      }
      v_push(rg->subgraphs, sg);
    }
  }
}

// ==========================================
// 6. Execute Logic
// ==========================================

void
rg_execute(RenderGraph *rg, VkCommandBuffer cmd)
{
  for(size_t i = 0; i < v_len(rg->subgraphs); i++) {
    RGSubgraph *sg = &rg->subgraphs[i];

    for(size_t t = 0; t < v_len(sg->tasks); t++) {
      RGTaskBatch *batch = &sg->tasks[t];
      RGTask      *task  = &rg->tasks[batch->taskId];

      if(v_len(batch->bufferBarriers) > 0 || v_len(batch->imageBarriers) > 0) {
        VkDependencyInfo dep = { .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .bufferMemoryBarrierCount = (uint32_t)v_len(batch->bufferBarriers),
                                 .pBufferMemoryBarriers    = batch->bufferBarriers,
                                 .imageMemoryBarrierCount  = (uint32_t)v_len(batch->imageBarriers),
                                 .pImageMemoryBarriers     = batch->imageBarriers };
        vkCmdPipelineBarrier2(cmd, &dep);
      }

      if(task->execute) {
        task->execute(cmd, task->user_data);
      }
    }
  }
}