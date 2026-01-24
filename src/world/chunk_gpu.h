
#pragma once
#include "resource/rm_internal.h"
#include "transfer_queue.h"
#include <stdbool.h>
#include <stdint.h>

#include "res_async.h"
#include "shaders/rt/rt_shared.glsl"
#include "world/chunk_internal.h"

typedef struct Device Device;
typedef struct ResourceManager ResourceManager;
typedef struct CmdBuffer CmdBuffer;
typedef struct ChunkUploadView ChunkUploadView;
typedef struct ChunkTree ChunkTree;

// ===========================
// Resource kinds (match CPU UploadPlan kinds)
// ===========================

// ===========================
// GPU upload state machine
// ===========================
typedef enum ChunkGpuUploadState {
  CHUNK_GPU_UPLOAD_IDLE = 0,      // nothing in flight
  CHUNK_GPU_UPLOAD_IN_FLIGHT,     // copies submitted, waiting on fence
  CHUNK_GPU_UPLOAD_READY_TO_SWAP, // fence signaled, can swap active buffers
  CHUNK_GPU_UPLOAD__COUNT,
} ChunkGpuUploadState;

typedef struct ChunkGpuBuffer {
  AsyncBuffer async;
  u32 gpu_version;    // last build_version that is fully active
  Vector queued_copy; //
} ChunkGpuBuffer;

// ===========================
// GPU resource container (double-buffered per resource kind)
// ===========================
typedef struct ChunkGpu {
  ChunkGpuBuffer buffers[CHUNK_RES__COUNT];
  GPUGridSlot stable_slots;

  uint32_t pending_mask; // what kinds are being uploaded
  Ticket pending_ticket;
} ChunkGpu;

// PUBLIC FUNCTIONS
void chunk_gpu_deinit(ChunkGpu *cg, ResourceManager *rm);
bool chunk_gpu_enqueue_upload(ChunkGpu *cg, M_Resource *rm, TransferQueue *transfer,
                              const ChunkUploadView *upload_data);

GPUGridSlot chunk_gpu_get_descriptor_indices(ChunkGpu *cg, M_Resource *rm);
ChunkGpu *chunk_gpu_init(M_Resource *rm, ChunkUploadView view);
ChunkGpuUploadState chunk_gpu_state(const ChunkGpu *cg, ChunkResType res_type);
ChunkGpuUploadState chunk_gpu_state_all(const ChunkGpu *cg);
bool chunk_gpu_tick(ChunkGpu *cg, M_Resource *rm, TransferQueue *queue, Vector *out_desc_updates);
// END PUBLIC FUNCTIONS
