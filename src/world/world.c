#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cglm/call/vec3.h"
#include "cglm/ivec3.h"
#include "cglm/types.h"
#include "cglm/vec3.h"
#include "common.h"
#include "iterator.h"
#include "resource/resmanager.h"
#include "resource/rm_internal.h"
#include "transfer_queue.h"
#include "util.h"
#include "vector.h"
#include "world/chunk.h"
#include "world/chunk_gpu.h"
#include "world/storage.h"

#define MAX_CHUNK_VISIBILITY 4

typedef ivec3 GridSlot;

typedef struct ChunkDelta {
  ivec3 dir;
} ChunkDelta;

typedef struct Range{

}Range;

typedef struct GridResult {
  Vector desc_dirty_indices; // u32[]
  Vector loaded_chunks; // vec3[]
  Vector unloaded_chunks; //u32[]

  Vector internal_indices_loaded; // u32[]
} GridResult;

typedef struct Grid {
  vec3 min_corner;
  i32 chunks[MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY];
  GPUGridSlot gpu_indices[MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY];
} Grid;

struct World {
  WorldConfig cfg;
  Grid grid;
  ChunkStore chunks;
  GridSlot prev_player;
  GridResult grid_result;

  ResHandle gpu_grid_indices;
};

// --- Private Prototypes ---
static int _get_slot_index(GridSlot slot);

static void _world_coord_to_chunk_pos(const World *w, vec3 player_pos, GridSlot out_chunk);

static void grid_init(Grid *grid, vec3 min_corner, GridResult *result);
static void grid_step(Grid *grid, ChunkDelta arrow, GridResult *result);

static i32 grid_get_chunk_idx(i32 *chunk_idxs, GridSlot grid_slot);
static void grid_get_world_coords(Grid *grid, GridSlot slot, vec3 coords_out);

static void grid_set_chunk_idx(i32 *chunk_idxs, GridSlot slot, u32 new_idx);
static void grid_set_descriptor_index(GPUGridSlot *indices, GridSlot src_slot, GridSlot dst_slot);

World *world_create(const WorldConfig *cfg, vec3 player_pos) {
  if (!cfg || cfg->visibility == 0 || cfg->chunk_size == 0)
    return NULL;

  World *w = (World *)calloc(1, sizeof(World));
  if (!w)
    return NULL;
  w->cfg = *cfg;
  
  vec_init(&w->grid_result.desc_dirty_indices, sizeof(u32), NULL);
  vec_init(&w->grid_result.unloaded_chunks, sizeof(u32), NULL);
  vec_init(&w->grid_result.internal_indices_loaded, sizeof(u32), NULL);
  vec_init(&w->grid_result.loaded_chunks, sizeof(vec3), NULL);
  
  _world_coord_to_chunk_pos(w, player_pos, w->prev_player);

  // init chunk storage/residency
  if (chunk_store_init(&w->chunks, cfg->max_cached) != CHUNK_STORE_OK) {
    free(w);
    return NULL;
  }
  grid_init(&w->grid, (float *)cfg->min_corner, &w->grid_result);
  // chunk has to take the chunks that got updated and create them
  // then gpu storage has to also add them as pending
  // then the grid updates the descriptor indices
  

  

  return w;
}

void world_init_gpu(World *w, M_Resource *rm, TransferQueue *transfer) {
  RGBufferInfo info = {.capacity = sizeof(w->grid.gpu_indices), .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
  info.queue_type = BUFFER_QUEUE_ALL;
  rm_create_buffer(rm, &info);
  chunk_store_gpu_tick(&w->chunks, rm, transfer);
  
}

void world_destroy(World *w) {
  if (!w)
    return;
  chunk_store_destroy(&w->chunks);
  free(w);
}

void world_grid_get_min_corner(World *w, vec3 out_min_corner) { glm_vec3_copy(w->grid.min_corner, out_min_corner); }
i32 world_grid_get_push_id(World *w, M_Resource *rm) {
  return rm_get_buffer_descriptor_index(rm, w->gpu_grid_indices);
}

void world_cpu_tick(World *w, vec3 player_pos) {
  if (!w)
    return;

  GridSlot curr_player_pos;
  _world_coord_to_chunk_pos(w, player_pos, curr_player_pos);

  GridSlot chunk_delta = {};
  glm_ivec3_sub(w->prev_player, curr_player_pos, chunk_delta);
  bool is_zero = glm_ivec3_dot(chunk_delta, chunk_delta);

  // has moved beyond a chunk
  if (!is_zero) {
    ChunkDelta delta = {};
    glm_ivec3_copy(delta.dir, chunk_delta);
    grid_step(&w->grid,  delta, &w->grid_result);

    // chunk has to take the chunks that got updated and create them
    // then gpu storage has to also add them as pending
    // then the grid updates the descriptor indices

  }

  // iterate active chunks
}

// --- Private Functions ---

static int _get_slot_index(GridSlot slot) {
  return slot[0] + slot[1] * MAX_CHUNK_VISIBILITY + slot[2] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY;
}

static void _world_coord_to_chunk_pos(const World *w, vec3 player_pos, GridSlot out_chunk) {
  vec3 rel;
  glm_vec3_sub(player_pos, (float *)w->cfg.min_corner, rel);
  out_chunk[0] = (int)floorf(rel[0] / (float)w->cfg.chunk_size);
  out_chunk[1] = (int)floorf(rel[1] / (float)w->cfg.chunk_size);
  out_chunk[2] = (int)floorf(rel[2] / (float)w->cfg.chunk_size);
}

static void grid_init(Grid *grid, vec3 min_corner, GridResult *result) {
  memcpy(grid->min_corner, min_corner, sizeof(vec3));
  Vector loaded_coords = {};
  vec_init_with_capacity(&loaded_coords, sizeof(vec3),
                         MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY, NULL);

  DEFER_VEC(&loaded_coords);

  for (u32 z = 0; z < MAX_CHUNK_VISIBILITY; z++) {
    for (u32 y = 0; y < MAX_CHUNK_VISIBILITY; y++) {
      for (u32 x = 0; x < MAX_CHUNK_VISIBILITY; x++) {
        GridSlot curr_slot = {x, y, z};
        vec3 world_coord_slot = {};
        grid_get_world_coords(grid, curr_slot, world_coord_slot);
        vec_push(&result->loaded_chunks, &world_coord_slot);

         i32 curr_index = grid_get_chunk_idx(grid->chunks, curr_slot);
        vec_push(&result->loaded_chunks, &curr_index);
      }
    }
  }
  //TODO, move it outside
  // ChunkApplyResult result_entered = chunk_store_apply_entered(store, &loaded_coords);

  // u32 counter = 0;
  // for (u32 z = 0; z < MAX_CHUNK_VISIBILITY; z++) {
  //   for (u32 y = 0; y < MAX_CHUNK_VISIBILITY; y++) {
  //     for (u32 x = 0; x < MAX_CHUNK_VISIBILITY; x++) {
  //       GridSlot curr_slot = {x, y, z};

  //       i32 active_idx = *VEC_AT(&result_entered.chunk_idxs, counter, u32);
  //       grid_set_chunk_idx(grid->chunks, curr_slot, active_idx);
  //       i32 idx = _get_slot_index(curr_slot);
  //       vec_push(&result->desc_dirty_indices, &idx);
  //       counter += 1;
  //     }
  //   }
  // }
}

static void grid_step(Grid *grid, ChunkDelta arrow, GridResult* result) {
  ivec3 dir_delta = {};
  glm_ivec3_copy(dir_delta, arrow.dir);
  i32 axis = 0;
  axis = dir_delta[1] != 0 ? 1 : 2;

  int N = MAX_CHUNK_VISIBILITY;

  ivec2 axis_loaded[3] = {{0, N}, {0, N}, {0, N}};
  ivec2 axis_unloaded[3] = {{0, N}, {0, N}, {0, N}};
  ivec2 axis_move[3] = {{0, N}, {0, N}, {0, N}};

  int step = (int)dir_delta[axis];

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
  //
  Vector unloaded_idx = {};

  vec_init_with_capacity(&unloaded_idx, sizeof(i32), MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY, NULL);
  DEFER_VEC(&unloaded_idx);

  // UNLOAD CHUNKS
  for (u32 z = axis_unloaded[2][0]; z < axis_unloaded[2][1]; z++) {
    for (u32 y = axis_unloaded[1][0]; y < axis_unloaded[1][1]; y++) {

      for (u32 x = axis_unloaded[0][0]; x < axis_unloaded[0][1]; x++) {
        GridSlot curr_slot = {x, y, z}; // CACHE THIS SLOT
        i32 curr_idx = grid_get_chunk_idx(grid->chunks, curr_slot);
        vec_push(&unloaded_idx, &curr_idx);
      }
    }
  }

  // MOVE CHUNKS
  for (u32 z = axis_move[2][0]; z < axis_move[2][1]; z++) {
    for (u32 y = axis_move[1][0]; y < axis_move[1][1]; y++) {

      for (u32 x = axis_move[0][0]; x < axis_move[0][1]; x++) { // Set the NExt Slot to current slot
        GridSlot curr_slot = {x, y, z};
        GridSlot next_slot = {};
        glm_ivec3_add(curr_slot, dir_delta, next_slot);
        i32 next_idx = grid_get_chunk_idx(grid->chunks, next_slot);
        grid_set_chunk_idx(grid->chunks, curr_slot, next_idx);
        grid_set_descriptor_index(grid->gpu_indices, next_slot, curr_slot);
      }
    }
  }

  for (u32 z = axis_loaded[2][0]; z < axis_loaded[2][1]; z++) {
    for (u32 y = axis_loaded[1][0]; y < axis_loaded[1][1]; y++) {
      for (u32 x = axis_loaded[0][0]; x < axis_loaded[0][1]; x++) {
        GridSlot curr_slot = {x, y, z};
        GridSlot next_slot = {};
        glm_ivec3_add(curr_slot, dir_delta, next_slot);
        vec3 world_coord_slot = {};
        grid_get_world_coords(grid, next_slot, world_coord_slot);
        vec_push(&result->loaded_chunks, &world_coord_slot);

        i32 curr_index = grid_get_chunk_idx(grid->chunks, curr_slot);
        vec_push(&result->loaded_chunks, &curr_index);
      }
    }
  }
  //TODO HAS TO MOVE THIS OUTSIDE

  // // APPLY CHANGES TO  STORAGE
  // ChunkApplyResult result_entered = chunk_store_apply_entered(store, &loaded_coords);
  // chunk_store_apply_left_idxs(store, unloaded_idx);

  // DEFER_VEC(&result_entered.chunk_idxs);

  // u32 counter = 0;
  // // LOADED CHUNKS GET UPDATED
  // for (u32 z = axis_loaded[2][0]; z < axis_loaded[2][1]; z++) {
  //   for (u32 y = axis_loaded[1][0]; y < axis_loaded[1][1]; y++) {
  //     for (u32 x = axis_loaded[0][0]; x < axis_loaded[0][1]; x++) {
  //       i32 active_idx = *VEC_AT(&result_entered.chunk_idxs, counter, u32);
  //       GridSlot curr_slot = {x, y, z};
  //       grid_set_chunk_idx(grid->chunks, curr_slot, active_idx);
  //       // TODO, return a result with indices that need to be checked
  //       counter += 1;
  //     }
  //   }
  // }
}

static i32 grid_get_chunk_idx(i32 *chunk_idxs, GridSlot grid_slot) {
  return chunk_idxs[grid_slot[0] + grid_slot[1] * MAX_CHUNK_VISIBILITY +
                    grid_slot[2] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY];
}

static void grid_get_world_coords(Grid *grid, GridSlot slot, vec3 coords_out) {
  float slot_x = slot[0] * CHUNK_SIZE;
  float slot_y = slot[1] * CHUNK_SIZE;
  float slot_z = slot[2] * CHUNK_SIZE;

  coords_out[0] = slot_x + grid->min_corner[0];
  coords_out[1] = slot_x + grid->min_corner[1];
  coords_out[2] = slot_x + grid->min_corner[2];
}

static void grid_set_chunk_idx(i32 *chunk_idxs, GridSlot slot, u32 new_idx) {
  chunk_idxs[slot[0] + slot[1] * MAX_CHUNK_VISIBILITY + slot[2] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY] =
      new_idx;
}

static void grid_set_descriptor_index(GPUGridSlot *indices, GridSlot src_slot, GridSlot dst_slot) {
  auto src_idx = _get_slot_index(src_slot);
  auto dst_idx = _get_slot_index(dst_slot);

  indices[dst_idx] = indices[src_idx];
}
