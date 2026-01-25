//! chunk_api.h

#include "chunk_api.h"
#include "chunk_internal.h"
#include "command.h"
#include "common.h"
#include "res_async.h"
#include "resource/resmanager.h"
#include "resource/rm_internal.h"
#include "rt/rt_shared.glsl"
#include "transfer_queue.h"
#include "vector.h"

// --- Private Prototypes ---
void chunk_gpu_init(ChunkGPUInput input, M_Resource *rm, TransferQueue *transfer, const ChunkUploadView *views) {
  for (u32 i = 0; i < input.indices.length; i++) {
    u32 chunk_idx = *VEC_AT(&input.indices, i, u32);
    ChunkGPU *chunk = VEC_AT(&input.chunk, i, ChunkGPU);

    if (chunk->is_init)
      continue;

    RGBufferInfo node_info = {.name = "NodeBuffer",
                              .capacity = vec_len(&views[i].p_res_data[CHUNK_RES_NODES]),
                              .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              .queue_type = BUFFER_QUEUE_ALL};

    RGBufferInfo child_info = {.name = "childIndexBuffer",
                               .capacity = vec_len(&views[i].p_res_data[CHUNK_RES_CHILDREN]),
                               .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                               .queue_type = BUFFER_QUEUE_ALL};

    RGBufferInfo pal_info = {.name = "Palette",
                             .capacity = vec_len(&views[i].p_res_data[CHUNK_RES_PALETTE]),
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             .queue_type = BUFFER_QUEUE_ALL};

    RGBufferInfo leaf_info = {.name = "LeafMatBuffer",
                              .capacity = vec_len(&views[i].p_res_data[CHUNK_RES_LEAF_MATS]),
                              .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                              .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              .queue_type = BUFFER_QUEUE_ALL};

    async_init(rm, &node_info, &chunk->buffers[CHUNK_RES_NODES]);
    async_init(rm, &child_info, &chunk->buffers[CHUNK_RES_CHILDREN]);
    async_init(rm, &node_info, &chunk->buffers[CHUNK_RES_PALETTE]);
    async_init(rm, &node_info, &chunk->buffers[CHUNK_RES_LEAF_MATS]);

    chunk->is_init = true;
  }

  chunk_gpu_upload(input, rm, transfer, views);
}

bool chunk_gpu_upload(ChunkGPUInput input, M_Resource *rm, TransferQueue *transfer,
                      const ChunkUploadView *upload_data) {
  for (u32 idx = 0; idx < input.indices.length; idx++) {
    u32 chunk_idx = *VEC_AT(&input.indices, idx, u32);
    ChunkGPU *chunk = VEC_AT(&input.chunk, idx, ChunkGPU);
    ChunkUploadView view = upload_data[idx];

    for (u32 res_idx = 0; res_idx < CHUNK_RES__COUNT; res_idx++) {
      Vector res_data = view.p_res_data[res_idx];

      if (res_data.length == 0)
        continue;

      transfer_push_upload(transfer, rm, async_get_backbuffer(&chunk->buffers[res_idx]), vec_bytes_len(&res_data),
                           res_data.data, 16);
    }
  }
  return true;
}

void chunk_gpu_get_descriptor_indices(ChunkGPUInput input, M_Resource *rm, GPUGridSlot *descriptor_slots) {
  for (u32 idx = 0; idx < input.indices.length; idx++) {
    u32 chunk_idx = *VEC_AT(&input.indices, idx, u32);
    if (chunk_idx == -1)
      continue;

    ChunkGPU *chunk = VEC_AT(&input.chunk, idx, ChunkGPU);

    descriptor_slots[idx].child_index_id = async_get_active_desc_index(&chunk->buffers[CHUNK_RES_CHILDREN], rm);
    descriptor_slots[idx].nodes_id = async_get_active_desc_index(&chunk->buffers[CHUNK_RES_NODES], rm);
    descriptor_slots[idx].leaf_mat_id = async_get_active_desc_index(&chunk->buffers[CHUNK_RES_LEAF_MATS], rm);
    descriptor_slots[idx].palette_id = async_get_active_desc_index(&chunk->buffers[CHUNK_RES_PALETTE], rm);
  }
}

void chunk_gpu_swap_buffers(ChunkGPUInput input, M_Resource *rm) {
  for (u32 idx = 0; idx < input.indices.length; idx++) {
    u32 chunk_idx = *VEC_AT(&input.indices, idx, u32);
    ChunkGPU *chunk = VEC_AT(&input.chunk, idx, ChunkGPU);

    for (u32 res_idx = 0; res_idx < CHUNK_RES__COUNT; res_idx++) {
      async_swap(rm, &chunk->buffers[res_idx]);
    }
  }
}

void chunk_gpu_deinit(ChunkGPUInput input, M_Resource *rm) {
  for (u32 idx = 0; idx < input.indices.length; idx++) {
    u32 chunk_idx = *VEC_AT(&input.indices, idx, u32);
    ChunkGPU *chunk = VEC_AT(&input.chunk, idx, ChunkGPU);
    for (u32 res_idx = 0; res_idx < CHUNK_RES__COUNT; res_idx++) {
      async_destroy(rm, &chunk->buffers[res_idx]);
    }
  }
  return;
}

// --- Private Functions ---
