#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cglm/ivec3.h"
#include "cglm/types.h"
#include "cglm/vec3.h"
#include "common.h"
#include "resource/resmanager.h"
#include "rt/rt_shared.glsl"
#include "transfer_queue.h"
#include "vector.h"

#include "internal.h"

#include "shared_types.h"
#include "world/chunk/chunk_api.h"
#include "world/chunk/chunk_internal.h"

// TODO something to implement, replacing indices, that should be uploaded again
// so if an index already exist, just replace

typedef struct ChunkDelta {
  ivec3 dir; // should be -1/0/+1 on a single axis
} ChunkDelta;

typedef struct WorldGPU {
  Vector chunks; // ChunkGPU

  // chunks that need to be uploaded
  ResHandle grid_indices;
  Vector need_transfer_idx; // u32[], index into active node
  Vector is_uploaded_idx;   // u32[], chunks that got uploaded
  bool is_pending;
  u64 submission_signal; // when pending is done
} WorldGPU;

struct World {
  WorldConfig cfg;

  ivec3 prev_player_chunk;

  i32 grid_idxs[GRID_VOL];

  Vector chunk_trees; // ChunkTree
  WorldGPU gpu;

  // one bindless patch batch per frame
  Vector desc_updates; // DescriptorInfo[]

  // GPU storage buffer containing GPUGridSlot[GRID_VOL]

  GridResult grid_result;

  ivec3 min_corner;

  Vector scratch_indices; // u32[]
};

// --- Private Prototypes ---
static int _slot_index(GridSlot slot);
static void _world_coord_to_chunk_pos(const World *w, vec3 player_pos, ivec3 out_chunk);
static void _chunk_coord_to_world_pos(const WorldConfig *cfg, const ivec3 chunk_coord, vec3 out_world_pos);

// ------------------------------------------------------------

World *world_create(const WorldConfig *cfg, vec3 player_pos) {
  if (!cfg || cfg->visibility == 0 || cfg->chunk_size == 0)
    return NULL;

  World *w = (World *)calloc(1, sizeof(World));
  if (!w)
    return NULL;

  w->cfg = *cfg;

  // result vectors
  vec_init(&w->grid_result.load_coords, sizeof(ivec3), NULL);
  vec_init(&w->grid_result.load_slots, sizeof(u32), NULL);
  vec_init(&w->grid_result.unload_coords, sizeof(ivec3), NULL);

  vec_init(&w->desc_updates, sizeof(DescriptorInfo), NULL);

  // init player chunk coord + grid window
  _world_coord_to_chunk_pos(w, player_pos, w->prev_player_chunk);

  // fill load list for initial grid and load CPU chunks
  GridInput grid_input = (GridInput){.slots = w->grid_idxs};
  memcpy(grid_input.min_corner, w->min_corner, sizeof(vec3));

  grid_init(&grid_input, w->prev_player_chunk, &w->grid_result);

  //   CHUNKS THAT NEED TO BE LOADED IN
  vec_clear(&w->scratch_indices);

  u32 old_len = vec_append_zero(&w->chunk_trees, w->grid_result.load_slots.length);

  for (u32 i = 0; i < w->grid_result.load_slots.length; i++) {
    u32 active_idx = old_len + i;
    vec_push(&w->scratch_indices, &active_idx);
  }

  ChunkInput chunk_input = {};
  chunk_input.indices = w->scratch_indices;
  chunk_input.trees = w->chunk_trees;

  chunk_init(chunk_input, w->grid_result.load_coords);

  vec_clear(&w->gpu.need_transfer_idx);

  return w;
}

void world_init_gpu(World *w, M_Resource *rm, TransferQueue *transfer) {
  if (!w)
    return;

  assert(!transfer_in_flight(transfer));

  RGBufferInfo info = {
      .capacity = GRID_VOL * sizeof(i32),
      .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .queue_type = BUFFER_QUEUE_ALL,
  };

  w->gpu.grid_indices = rm_create_buffer(rm, &info);
  ChunkGPUInput gpu_input = {.chunk = w->gpu.chunks, .indices = w->gpu.need_transfer_idx};
  ChunkInput chunk_input_view = {.trees = w->chunk_trees, .indices = w->gpu.need_transfer_idx};

  // TODO, maybe memset
  ChunkUploadView views[w->gpu.need_transfer_idx.length];
  chunk_get_upload_view(chunk_input_view, views);
  chunk_gpu_init(gpu_input, rm, transfer, views);

  // ok to upload zeros; first world_gpu_tick will rebuild real values
  transfer_push_upload(transfer, rm, w->gpu.grid_indices, GRID_VOL * sizeof(u32), &w->grid_idxs, 16);

  // move the indices to uploaded list
  vec_copy(&w->gpu.is_uploaded_idx, &w->gpu.need_transfer_idx);
  vec_clear(&w->gpu.need_transfer_idx);
}

void world_destroy(World *w) {
  if (!w)
    return;

  vec_free(&w->grid_result.load_coords);
  vec_free(&w->grid_result.load_slots);
  vec_free(&w->grid_result.unload_coords);

  vec_free(&w->desc_updates);

  free(w);
}

void world_grid_get_min_corner(World *w, vec3 out_min_corner) {
  if (!w)
    return;

  // world-space min corner of the whole grid window
  ivec3 origin = {};
  glm_ivec3_copy(w->grid.origin_chunk, origin);

  vec3 origin_world = {};
  _chunk_coord_to_world_pos(&w->cfg, origin, origin_world);
  glm_vec3_copy(origin_world, out_min_corner);
}

i32 world_grid_get_push_id(World *w, M_Resource *rm) {
  if (!w)
    return 0;
  return (i32)rm_get_buffer_descriptor_index(rm, w->gpu_grid_indices);
}

// ------------------------------------------------------------
// GPU TICK
// ------------------------------------------------------------
//
// NOTE: This assumes you changed chunk_gpu_tick() to append DescriptorInfo updates:
//   bool chunk_gpu_tick(ChunkGpu*, M_Resource*, TransferQueue*, Vector* out_desc_updates);
//
// If you haven't yet, either do that change OR remove &w->desc_updates in the call.
//
void world_gpu_tick(World *w, M_Resource *rm, TransferQueue *transfer) {
  if (!w)
    return;

  if (transfer_in_flight(transfer))
    return;

  // TODO, check uploaded list, then swap all of them.
  // TODO, check transfer list, then upload and put on upload list
  // TODO, upload grid indices if dirty
}

// ------------------------------------------------------------
// CPU TICK
// ------------------------------------------------------------
void world_cpu_tick(World *w, vec3 player_pos) {
  if (!w)
    return;

  // TODO, do grid_step
  // TODO, Add any need load chunks to transfer list
  // TODO, if grid updated, then also set it dirty

  ivec3 curr_player_chunk = {};
  _world_coord_to_chunk_pos(w, player_pos, curr_player_chunk);

  ivec3 delta = {};
  glm_ivec3_sub(curr_player_chunk, w->prev_player_chunk, delta);

  if (glm_ivec3_dot(delta, delta) == 0) {
    // still same chunk
    return;
  }

  // handle multi-chunk jumps safely, one step at a time
  ivec3 remaining = {};
  glm_ivec3_copy(delta, remaining);

  while (remaining[0] != 0 || remaining[1] != 0 || remaining[2] != 0) {
    ChunkDelta step = {0};

    // choose first axis that moved
    int axis = (remaining[0] != 0) ? 0 : (remaining[1] != 0) ? 1 : 2;

    int s = (remaining[axis] > 0) ? 1 : -1;
    step.dir[0] = 0;
    step.dir[1] = 0;
    step.dir[2] = 0;
    step.dir[axis] = s;

    // clear grid results
    vec_clear(&w->grid_result.load_coords);
    vec_clear(&w->grid_result.load_slots);
    vec_clear(&w->grid_result.unload_coords);

    // step the grid window (produces load/unload coord lists)
    grid_step(&w->grid, step, &w->grid_result);

    // unload -> cache by COORD (robust, avoids active_pos issues)
    chunk_store_apply_left_to_cache(&w->chunks, w->grid_result.unload_coords, &w->store_result);

    // load new coords (CPU)
    chunk_store_load(&w->chunks, w->grid_result.load_coords, NULL, &w->store_result);

    // assign loaded chunks into grid slots + queue uploads
    for (u32 i = 0; i < (u32)vec_len(&w->store_result.chunk_idxs); i++) {
      u32 active_pos = *VEC_AT(&w->store_result.chunk_idxs, i, u32);
      u32 slot_idx = *VEC_AT(&w->grid_result.load_slots, i, u32);

      u32 chunk_index = *VEC_AT(&w->chunks.active_chunk_indices, active_pos, u32);
      w->grid.chunks[slot_idx] = (i32)chunk_index;

      ChunkTree *t = chunk_store_chunk_at(&w->chunks, chunk_index);
      chunk_build_if_needed(t);

      ChunkUploadReq req = {
          .chunk_index = chunk_index,
          .version = chunk_build_version(t),
          .res_mask = 0xFFFFFFFFu,
      };
      vec_push(&w->upload_q, &req);
    }

    w->grid_gpu_dirty = true;

    // advance player chunk one step
    w->prev_player_chunk[axis] += s;
    remaining[axis] -= s;
  }

  // optional: handle edits / dirty chunks while active
  for (u32 i = 0; i < (u32)vec_len(&w->chunks.active_chunk_indices); i++) {
    u32 chunk_index = *VEC_AT(&w->chunks.active_chunk_indices, i, u32);
    ChunkTree *t = chunk_store_chunk_at(&w->chunks, chunk_index);

    if (chunk_is_dirty(t)) {
      chunk_build_if_needed(t);

      ChunkUploadReq req = {
          .chunk_index = chunk_index,
          .version = chunk_build_version(t),
          .res_mask = 0xFFFFFFFFu,
      };
      vec_push(&w->upload_q, &req);
    }
  }
}

// ------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------
