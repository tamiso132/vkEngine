#include "common.h"
#include <cglm/cglm.h>

#include "../shared_types.h"

#include "res_async.h"
#include "transfer_queue.h"

typedef enum ChunkResType {
  CHUNK_RES_NODES = 0,
  CHUNK_RES_CHILDREN,
  CHUNK_RES_PALETTE,
  CHUNK_RES_LEAF_MATS,
  CHUNK_RES__COUNT,
} ChunkResType;

typedef struct ChunkGPU {
  AsyncBuffer buffers[CHUNK_RES__COUNT];
  bool is_init;
} ChunkGPU;

typedef struct ChunkGPUInput {
  Vector chunk;   // ChunkGPU[]
  Vector indices; // u32[]
} ChunkGPUInput;

typedef struct ChunkUploadView {
  // Node[]
  // ChildIndex[]
  // u32 RGBA[]
  // u16 materials[]
  Vector p_res_data[CHUNK_RES__COUNT];
} ChunkUploadView;

typedef struct ChunkCreateInfo {
  vec3 *min_corner;
  u32 count;
} ChunkCreateInfo;

typedef struct ChunkResult {
  Vector out_chunks; // chunks[]
} ChunkResult;

typedef struct ChunkInput {
  Vector trees;   // Chunktree[]
  Vector indices; // affected indices
} ChunkInput;

struct ChunkTree {
  ivec3 min_corner;
  u64 bits[(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE + 63) / 64];
  u16 vox_mat[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];

  ChunkUploadView view[CHUNK_RES__COUNT];
};

// PUBLIC FUNCTIONS
void chunk_destroy(ChunkInput *chunks);
bool chunk_get_upload_view(const ChunkInput input, ChunkUploadView *out_view);
void chunk_gpu_deinit(ChunkGPUInput input, M_Resource *rm);
void chunk_gpu_get_descriptor_indices(ChunkGPUInput input, M_Resource *rm, GPUGridSlot *descriptor_slots);
void chunk_gpu_init(ChunkGPUInput input, M_Resource *rm, TransferQueue *transfer, const ChunkUploadView *views);
void chunk_gpu_swap_buffers(ChunkGPUInput input, M_Resource *rm);
bool chunk_gpu_upload(ChunkGPUInput input, M_Resource *rm, TransferQueue *transfer, const ChunkUploadView *upload_data);
void chunk_rebuild_cpu(ChunkInput input);
void chunk_init(ChunkInput input, Vector global_coords);
void chunk_rebuild_cpu(ChunkInput input);
// END PUBLIC FUNCTIONS
