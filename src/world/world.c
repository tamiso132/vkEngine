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
#include "world/chunk.h"
#include "world/chunk_gpu.h"
#include "world/storage.h"

#define MAX_CHUNK_VISIBILITY 4
#define GRID_VOL (MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY)

typedef ivec3 GridSlot;

typedef struct ChunkUploadReq {
  u32 chunk_index;
  u64 version;
  u32 res_mask;
} ChunkUploadReq;

typedef struct ChunkDelta {
  ivec3 dir; // should be -1/0/+1 on a single axis
} ChunkDelta;

typedef struct GridResult {
  Vector load_coords;   // ivec3[]  (global chunk coords to load)
  Vector load_slots;    // u32[]    (slot indices for those coords)
  Vector unload_coords; // ivec3[]  (global chunk coords to unload/cache)
} GridResult;

typedef struct Grid {
  // global chunk coordinate at slot (0,0,0)
  ivec3 origin_chunk;

  // chunk_index per slot, -1 = empty
  i32 chunks[GRID_VOL];

  // shader-facing descriptor indices per slot
  GPUGridSlot gpu_indices[GRID_VOL];
} Grid;

struct World {
  WorldConfig cfg;

  Grid grid;
  ChunkStore chunks;

  ivec3 prev_player_chunk;

  // CPU -> GPU queue
  Vector upload_q; // ChunkUploadReq[]

  // one bindless patch batch per frame
  Vector desc_updates; // DescriptorInfo[]

  // GPU storage buffer containing GPUGridSlot[GRID_VOL]
  ResHandle gpu_grid_indices;
  bool grid_gpu_dirty;

  GridResult grid_result;
  ChunkStoreResult store_result;
};

// --- Private Prototypes ---
static int _slot_index(GridSlot slot);
static void _world_coord_to_chunk_pos(const World *w, vec3 player_pos, ivec3 out_chunk);
static void _chunk_coord_to_world_pos(const WorldConfig *cfg, const ivec3 chunk_coord, vec3 out_world_pos);

static void grid_init(Grid *grid, const ivec3 player_chunk, GridResult *result);
static void grid_step(Grid *grid, ChunkDelta delta, GridResult *result);

static inline i32 grid_get_chunk_index(const Grid *grid, GridSlot slot);
static inline void grid_set_chunk_index(Grid *grid, GridSlot slot, i32 chunk_index);

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

  vec_init(&w->upload_q, sizeof(ChunkUploadReq), NULL);
  vec_init(&w->desc_updates, sizeof(DescriptorInfo), NULL);

  vec_init(&w->store_result.chunk_idxs, sizeof(u32), NULL);

  // init chunk storage/residency
  chunk_store_init(&w->chunks, cfg->max_cached, &w->store_result);

  // init player chunk coord + grid window
  _world_coord_to_chunk_pos(w, player_pos, w->prev_player_chunk);

  // clear grid
  memset(w->grid.chunks, 0xFF, sizeof(w->grid.chunks)); // -1
  memset(w->grid.gpu_indices, UNDEFINED_VALUE, sizeof(w->grid.gpu_indices));

  // fill load list for initial grid and load CPU chunks
  grid_init(&w->grid, w->prev_player_chunk, &w->grid_result);
  chunk_store_load(&w->chunks, w->grid_result.load_coords, NULL, &w->store_result);

  // map loaded results into grid slots + queue initial uploads
  for (u32 i = 0; i < (u32)vec_len(&w->store_result.chunk_idxs); i++) {
    u32 active_pos = *VEC_AT(&w->store_result.chunk_idxs, i, u32);
    u32 slot_idx = *VEC_AT(&w->grid_result.load_slots, i, u32);

    // stable chunk_index for this active_pos
    u32 chunk_index = *VEC_AT(&w->chunks.active_chunk_indices, active_pos, u32);

    w->grid.chunks[slot_idx] = (i32)chunk_index;

    // ensure CPU data exists
    ChunkTree *t = chunk_store_chunk_at(&w->chunks, chunk_index);
    chunk_build_if_needed(t);

    // queue upload for GPU tick
    ChunkUploadReq req = {
        .chunk_index = chunk_index,
        .version = chunk_build_version(t),
        .res_mask = 0xFFFFFFFFu,
    };
    vec_push(&w->upload_q, &req);
  }

  w->grid_gpu_dirty = true;
  return w;
}

void world_init_gpu(World *w, M_Resource *rm, TransferQueue *transfer) {
  if (!w)
    return;

  RGBufferInfo info = {
      .capacity = sizeof(w->grid.gpu_indices),
      .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .queue_type = BUFFER_QUEUE_ALL,
  };

  w->gpu_grid_indices = rm_create_buffer(rm, &info);

  // ok to upload zeros; first world_gpu_tick will rebuild real values
  transfer_push_upload(transfer, rm, w->gpu_grid_indices, sizeof(w->grid.gpu_indices), w->grid.gpu_indices, 16);

  w->grid_gpu_dirty = true;
}

void world_destroy(World *w) {
  if (!w)
    return;

  chunk_store_destroy(&w->chunks);

  vec_free(&w->grid_result.load_coords);
  vec_free(&w->grid_result.load_slots);
  vec_free(&w->grid_result.unload_coords);

  vec_free(&w->upload_q);
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

  vec_clear(&w->desc_updates);

  // 1) Drain upload requests (CPU -> GPU)
  for (u32 i = 0; i < (u32)vec_len(&w->upload_q); i++) {
    ChunkUploadReq *req = VEC_AT(&w->upload_q, i, ChunkUploadReq);
    ChunkItem *item = VEC_AT(&w->chunks.chunk_items, req->chunk_index, ChunkItem);

    // ensure GPU chunk exists
    if (!item->chunk_gpu) {
      ChunkUploadView view = {0};
      if (chunk_get_upload_view(item->tree, &view)) {
        item->chunk_gpu = chunk_gpu_init(rm, view);
      } else {
        continue;
      }
    }

    // enqueue upload to back buffer
    ChunkUploadView view = {0};
    if (chunk_get_upload_view(item->tree, &view)) {
      chunk_gpu_enqueue_upload(item->chunk_gpu, rm, transfer, &view);
    }
  }
  vec_clear(&w->upload_q);

  // 2) Tick active chunks -> append descriptor patch info when swaps complete
  for (u32 i = 0; i < (u32)vec_len(&w->chunks.active_chunk_indices); i++) {
    u32 chunk_index = *VEC_AT(&w->chunks.active_chunk_indices, i, u32);
    ChunkItem *item = VEC_AT(&w->chunks.chunk_items, chunk_index, ChunkItem);
    if (!item->chunk_gpu)
      continue;

    // changed signature version:
    chunk_gpu_tick(item->chunk_gpu, rm, transfer, &w->desc_updates);
  }

  // 3) Flush one bindless update batch per frame
  if (vec_len(&w->desc_updates) > 0) {
    rm_bindless_batch_buffer_update(rm, (DescriptorInfo *)w->desc_updates.data, (u32)w->desc_updates.length);
  }

  // 4) Rebuild + upload grid indices buffer only when dirty
  if (w->grid_gpu_dirty) {
    for (u32 slot = 0; slot < GRID_VOL; slot++) {
      i32 chunk_index = w->grid.chunks[slot];
      if (chunk_index < 0) {
        w->grid.gpu_indices[slot] = (GPUGridSlot){0};
        continue;
      }

      ChunkItem *item = VEC_AT(&w->chunks.chunk_items, (u32)chunk_index, ChunkItem);
      if (!item || !item->chunk_gpu) {
        w->grid.gpu_indices[slot] = (GPUGridSlot){0};
        continue;
      }

      w->grid.gpu_indices[slot] = chunk_gpu_get_descriptor_indices(item->chunk_gpu, rm);
    }

    transfer_push_upload(transfer, rm, w->gpu_grid_indices, sizeof(w->grid.gpu_indices), w->grid.gpu_indices, 16);

    w->grid_gpu_dirty = false;
  }
}

// ------------------------------------------------------------
// CPU TICK
// ------------------------------------------------------------
void world_cpu_tick(World *w, vec3 player_pos) {
  if (!w)
    return;

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

static int _slot_index(GridSlot slot) {
  return slot[0] + slot[1] * MAX_CHUNK_VISIBILITY + slot[2] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY;
}

static void _world_coord_to_chunk_pos(const World *w, vec3 player_pos, ivec3 out_chunk) {
  vec3 rel = {};
  glm_vec3_sub(player_pos, (float *)w->cfg.min_corner, rel);

  out_chunk[0] = (int)floorf(rel[0] / (float)w->cfg.chunk_size);
  out_chunk[1] = (int)floorf(rel[1] / (float)w->cfg.chunk_size);
  out_chunk[2] = (int)floorf(rel[2] / (float)w->cfg.chunk_size);
}

static void _chunk_coord_to_world_pos(const WorldConfig *cfg, const ivec3 chunk_coord, vec3 out_world_pos) {
  out_world_pos[0] = cfg->min_corner[0] + (float)chunk_coord[0] * (float)cfg->chunk_size;
  out_world_pos[1] = cfg->min_corner[1] + (float)chunk_coord[1] * (float)cfg->chunk_size;
  out_world_pos[2] = cfg->min_corner[2] + (float)chunk_coord[2] * (float)cfg->chunk_size;
}

static inline i32 grid_get_chunk_index(const Grid *grid, GridSlot slot) { return grid->chunks[_slot_index(slot)]; }

static inline void grid_set_chunk_index(Grid *grid, GridSlot slot, i32 chunk_index) {
  grid->chunks[_slot_index(slot)] = chunk_index;
}

static void grid_init(Grid *grid, const ivec3 player_chunk, GridResult *result) {
  // center-ish window: origin = player - N/2
  ivec3 origin = {
      player_chunk[0] - (MAX_CHUNK_VISIBILITY / 2),
      player_chunk[1] - (MAX_CHUNK_VISIBILITY / 2),
      player_chunk[2] - (MAX_CHUNK_VISIBILITY / 2),
  };
  glm_ivec3_copy(origin, grid->origin_chunk);

  vec_clear(&result->load_coords);
  vec_clear(&result->load_slots);
  vec_clear(&result->unload_coords);

  for (u32 z = 0; z < MAX_CHUNK_VISIBILITY; z++) {
    for (u32 y = 0; y < MAX_CHUNK_VISIBILITY; y++) {
      for (u32 x = 0; x < MAX_CHUNK_VISIBILITY; x++) {
        GridSlot slot = {(int)x, (int)y, (int)z};
        ivec3 coord = {origin[0] + (int)x, origin[1] + (int)y, origin[2] + (int)z};

        u32 slot_idx = (u32)_slot_index(slot);

        vec_push(&result->load_coords, &coord);
        vec_push(&result->load_slots, &slot_idx);
      }
    }
  }
}

static void grid_step(Grid *grid, ChunkDelta delta, GridResult *result) {
  // delta.dir should be +/-1 on a single axis
  ivec3 d = {};
  glm_ivec3_copy(delta.dir, d);

  int axis = -1;
  if (d[0] != 0)
    axis = 0;
  else if (d[1] != 0)
    axis = 1;
  else if (d[2] != 0)
    axis = 2;
  else
    return;

  int step = d[axis];
  if (step > 0)
    step = 1;
  if (step < 0)
    step = -1;

  ivec3 old_origin = {};
  glm_ivec3_copy(grid->origin_chunk, old_origin);

  ivec3 new_origin = {old_origin[0] + d[0], old_origin[1] + d[1], old_origin[2] + d[2]};
  glm_ivec3_copy(new_origin, grid->origin_chunk);

  vec_clear(&result->load_coords);
  vec_clear(&result->load_slots);
  vec_clear(&result->unload_coords);

  int N = MAX_CHUNK_VISIBILITY;

  ivec2 axis_loaded[3] = {{0, N}, {0, N}, {0, N}};
  ivec2 axis_unloaded[3] = {{0, N}, {0, N}, {0, N}};
  ivec2 axis_move[3] = {{0, N}, {0, N}, {0, N}};

  if (step == +1) {
    axis_unloaded[axis][0] = 0;
    axis_unloaded[axis][1] = 1;

    axis_loaded[axis][0] = N - 1;
    axis_loaded[axis][1] = N;

    axis_move[axis][0] = 0;
    axis_move[axis][1] = N - 1;
  } else { // step == -1
    axis_unloaded[axis][0] = N - 1;
    axis_unloaded[axis][1] = N;

    axis_loaded[axis][0] = 0;
    axis_loaded[axis][1] = 1;

    axis_move[axis][0] = 1;
    axis_move[axis][1] = N;
  }

  // 1) collect unload coords (from old origin + face slots)
  for (int z = axis_unloaded[2][0]; z < axis_unloaded[2][1]; z++) {
    for (int y = axis_unloaded[1][0]; y < axis_unloaded[1][1]; y++) {
      for (int x = axis_unloaded[0][0]; x < axis_unloaded[0][1]; x++) {
        ivec3 c = {old_origin[0] + x, old_origin[1] + y, old_origin[2] + z};
        vec_push(&result->unload_coords, &c);
      }
    }
  }

  // 2) move existing chunk_index mapping in the grid array
  for (int z = axis_move[2][0]; z < axis_move[2][1]; z++) {
    for (int y = axis_move[1][0]; y < axis_move[1][1]; y++) {
      for (int x = axis_move[0][0]; x < axis_move[0][1]; x++) {
        GridSlot curr = {x, y, z};
        GridSlot next = {x + d[0], y + d[1], z + d[2]};

        i32 moved_chunk_index = grid_get_chunk_index(grid, next);
        grid_set_chunk_index(grid, curr, moved_chunk_index);
      }
    }
  }

  // 3) mark loaded face slots empty + produce load coords for them (new origin + slot)
  for (int z = axis_loaded[2][0]; z < axis_loaded[2][1]; z++) {
    for (int y = axis_loaded[1][0]; y < axis_loaded[1][1]; y++) {
      for (int x = axis_loaded[0][0]; x < axis_loaded[0][1]; x++) {
        GridSlot slot = {x, y, z};
        u32 slot_idx = (u32)_slot_index(slot);

        ivec3 c = {new_origin[0] + x, new_origin[1] + y, new_origin[2] + z};

        grid_set_chunk_index(grid, slot, -1);
        vec_push(&result->load_coords, &c);
        vec_push(&result->load_slots, &slot_idx);
      }
    }
  }
}
