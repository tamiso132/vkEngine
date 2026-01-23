#include "chunk_gpu.h"
#include "chunk.h"
#include "chunk_internal.h"
#include "command.h"
#include "common.h"
#include "res_async.h"
#include "resource/rm_internal.h"
#include "rt/rt_shared.glsl"
#include "transfer_queue.h"
#include "vector.h"

u32 PENDING_BIT_MASK = (1 << (CHUNK_GPU_UPLOAD__COUNT)) - 1;

// --- Private Prototypes ---

static inline void _set_chunk_state(u32 *pending_mask, ChunkResType type, ChunkGpuUploadState new_state);

static inline u32 _get_pending_idle_mask();

static ChunkGpuUploadState _get_chunk_state(const u32 pending_mask, ChunkResType type);
static ChunkGpuUploadState _get_chunk_state_all(const u32 pending_mask);

ChunkGpu *chunk_gpu_init(M_Resource *rm, ChunkUploadView view) {
  ChunkGpu *cg = calloc(sizeof(ChunkGpu), 1);
  memset(cg, 0, sizeof(*cg));

  RGBufferInfo node_info = {.name = "NodeBuffer",
                            .capacity = vec_len(&view.p_res_data[CHUNK_RES_NODES]),
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo child_info = {.name = "childIndexBuffer",
                             .capacity = vec_len(&view.p_res_data[CHUNK_RES_CHILDREN]),
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo pal_info = {.name = "Palette",
                           .capacity = vec_len(&view.p_res_data[CHUNK_RES_PALETTE]),
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo leaf_info = {.name = "LeafMatBuffer",
                            .capacity = vec_len(&view.p_res_data[CHUNK_RES_LEAF_MATS]),
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  async_init(rm, &node_info, &cg->buffers[CHUNK_RES_NODES]->async);
  async_init(rm, &child_info, &cg->buffers[CHUNK_RES_CHILDREN]->async);
  async_init(rm, &node_info, &cg->buffers[CHUNK_RES_PALETTE]->async);
  async_init(rm, &node_info, &cg->buffers[CHUNK_RES_LEAF_MATS]->async);

  return cg;
}

void chunk_gpu_deinit(ChunkGpu *cg, ResourceManager *rm) {
  if (!cg)
    return;
  for (u32 k = 0; k < CHUNK_RES__COUNT; ++k) {
    if (cg->buffers[k]) {
      // TODO  async_destroy(rm, cg->buffers[k]);
      free(cg->buffers[k]);
      cg->buffers[k] = NULL;
    }
  }
  memset(cg, 0, sizeof(*cg));
}

// ============================================================
// query
// ============================================================
ChunkGpuUploadState chunk_gpu_state(const ChunkGpu *cg, ChunkResType res_type) { return cg->pending_mask; }

ChunkGpuUploadState chunk_gpu_state_all(const ChunkGpu *cg) { return _get_chunk_state_all(cg->pending_mask); }
GPUGridSlot chunk_gpu_get_descriptor_indices(ChunkGpu *cg, M_Resource *rm) {
  GPUGridSlot out = {0};
  if (!cg)
    return out;

  out.nodes_id = rm_get_buffer_index(rm, async_get_active_buffer(&cg->buffers[CHUNK_RES_NODES]->async));
  out.child_index_id = rm_get_buffer_index(rm, async_get_active_buffer(&cg->buffers[CHUNK_RES_CHILDREN]->async));
  out.palette_id = rm_get_buffer_index(rm, async_get_active_buffer(&cg->buffers[CHUNK_RES_PALETTE]->async));
  out.leaf_mat_id = rm_get_buffer_index(rm, async_get_active_buffer(&cg->buffers[CHUNK_RES_LEAF_MATS]->async));

  return out;
}

// ============================================================
// enqueue upload
// ============================================================
bool chunk_gpu_enqueue_upload(ChunkGpu *cg, ResourceManager *rm, TransferQueue *transfer,
                              const ChunkUploadView *upload_data) {

  const ChunkUploadView *ud = (const ChunkUploadView *)upload_data;

  const u32 aligment = 16;

  // Issue per-resource staging allocations + memcpy into staging
  for (u32 k = 0; k < CHUNK_RES__COUNT; ++k) {

    if (upload_data->p_res_data[k].length == 0)
      continue;

    AsyncBuffer *ab = &cg->buffers[k]->async;

    ResHandle back_buffer = async_get_backbuffer(ab);
    Vector upload = upload_data->p_res_data[k];

    cg->pending_ticket =
        transfer_push_upload(transfer, (M_Resource *)rm, back_buffer, vec_bytes_len(&upload), upload.data, 16);

    _set_chunk_state(&cg->pending_mask, k, CHUNK_GPU_UPLOAD_IN_FLIGHT);
  }

  return true;
}

// ============================================================
// poll + swap
// ============================================================
bool chunk_gpu_tick(ChunkGpu *cg, M_Resource *rm, TransferQueue *queue) {
  u32 idle_mask = _get_pending_idle_mask();
  if ((cg->pending_mask & idle_mask) == idle_mask)
    return true;

  u64 current_ticket = transfer_get_current_ticket_completed(queue);
  if (current_ticket >= cg->pending_ticket) {
    // TODO, ITERATE OVER ALL BUFFERS AND SWAP IF NOT IDLE
    // TODO, return some type of result, telling what descriptors are dirty
    for (u32 i = 0; i < CHUNK_RES__COUNT; i++) {
      u32 pending_state = _get_chunk_state(cg->pending_mask, i);
      if (pending_state != 1 << CHUNK_GPU_UPLOAD_IN_FLIGHT)
        continue;

      async_swap(rm, &cg->buffers[i]->async);
      _set_chunk_state(&cg->pending_mask, i, CHUNK_GPU_UPLOAD_IDLE);

      if (cg->buffers[i]->queued_copy.length != 0) {
        // TODO, Enqueue stuff here and set it to PENDING AGAIN
      }
    }
  }
  return true;
}

// --- Private Functions ---

static inline void _set_chunk_state(u32 *pending_mask, ChunkResType type, ChunkGpuUploadState new_state) {

  u32 shift_base = type * CHUNK_GPU_UPLOAD__COUNT;
  *pending_mask &= ~(PENDING_BIT_MASK << shift_base);
  *pending_mask |= (1 << (shift_base + new_state));
}

static inline u32 _get_pending_idle_mask() {
  u32 m = 0;
  for (u32 type = 0; type < (u32)CHUNK_RES__COUNT; ++type) {
    u32 shift_base = type * (u32)CHUNK_GPU_UPLOAD__COUNT;
    m |= (1u << shift_base); // idle is bit0 in each field
  }
  return m;
}

static ChunkGpuUploadState _get_chunk_state(const u32 pending_mask, ChunkResType type) {
  u32 shift_base = type * CHUNK_GPU_UPLOAD__COUNT;
  return (pending_mask >> shift_base) & PENDING_BIT_MASK;
}

static ChunkGpuUploadState _get_chunk_state_all(const u32 pending_mask) {
  ChunkGpuUploadState state = {};
  for (u32 i = 0; i < CHUNK_RES__COUNT; i++) {
    state |= _get_chunk_state(pending_mask, i);
  }
  return state;
}
