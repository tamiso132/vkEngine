//! internal.h
#include "cglm/ivec3.h"
#include "cglm/vec3.h"
#include "internal.h"
#include "rt/rt_shared.glsl"
#include "shared_types.h"

// --- Private Prototypes ---
static void _chunk_coord_to_world_pos(const GridInput *cfg, const ivec3 chunk_coord, vec3 out_world_pos);

static int _slot_index(GridSlot slot);

static void _world_coord_to_chunk_pos(const GridInput *input, vec3 player_pos, ivec3 out_chunk);

static inline i32 grid_get_chunk_index(const GridInput *grid, GridSlot slot);
static inline void grid_set_chunk_index(const GridInput *grid, GridSlot slot, i32 chunk_index);

void grid_init(GridInput *grid, const ivec3 player_chunk, GridResult *result) {
  // center-ish window: origin = player - N/2
  ivec3 origin = {
      player_chunk[0] - (MAX_CHUNK_VISIBILITY / 2),
      player_chunk[1] - (MAX_CHUNK_VISIBILITY / 2),
      player_chunk[2] - (MAX_CHUNK_VISIBILITY / 2),
  };
  glm_ivec3_copy(origin, grid->min_corner);

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

void grid_step(GridInput *input, ivec3 dir, GridResult *result) {
  // delta.dir should be +/-1 on a single axis
  ivec3 d = {};
  glm_ivec3_copy(dir, d);

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
        ivec3 c = {input->min_corner[0] + x, input->min_corner[1] + y, input->min_corner[2] + z};
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

        i32 moved_chunk_index = grid_get_chunk_index(input, next);
        grid_set_chunk_index(input, curr, moved_chunk_index);
      }
    }
  }

  // 3) mark loaded face slots empty + produce load coords for them (new origin + slot)
  for (int z = axis_loaded[2][0]; z < axis_loaded[2][1]; z++) {
    for (int y = axis_loaded[1][0]; y < axis_loaded[1][1]; y++) {
      for (int x = axis_loaded[0][0]; x < axis_loaded[0][1]; x++) {
        GridSlot slot = {x, y, z};
        u32 curr_slot = (u32)_slot_index(slot);

        ivec3 loaded_coords = {};
        glm_ivec3_add(slot, dir, loaded_coords);
        glm_ivec3_scale(loaded_coords, CHUNK_SIZE, loaded_coords);
        glm_ivec3_add(loaded_coords, input->min_corner, loaded_coords);

        grid_set_chunk_index(input, slot, -1);
        vec_push(&result->load_coords, &loaded_coords);
        vec_push(&result->load_slots, &curr_slot);
      }
    }
  }
}

// --- Private Functions ---

static void _chunk_coord_to_world_pos(const GridInput *cfg, const ivec3 chunk_coord, vec3 out_world_pos) {
  out_world_pos[0] = cfg->min_corner[0] + (float)chunk_coord[0] * CHUNK_SIZE;
  out_world_pos[1] = cfg->min_corner[1] + (float)chunk_coord[1] * CHUNK_SIZE;
  out_world_pos[2] = cfg->min_corner[2] + (float)chunk_coord[2] * CHUNK_SIZE;
}

static int _slot_index(GridSlot slot) {
  return slot[0] + slot[1] * MAX_CHUNK_VISIBILITY + slot[2] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY;
}

static void _world_coord_to_chunk_pos(const GridInput *input, vec3 player_pos, ivec3 out_chunk) {
  vec3 rel = {};
  glm_vec3_sub(player_pos, (float *)input->min_corner, rel);

  out_chunk[0] = (int)floorf(rel[0] / CHUNK_SIZE);
  out_chunk[1] = (int)floorf(rel[1] / CHUNK_SIZE);
  out_chunk[2] = (int)floorf(rel[2] / CHUNK_SIZE);
}

static inline i32 grid_get_chunk_index(const GridInput *grid, GridSlot slot) { return grid->slots[_slot_index(slot)]; }

static inline void grid_set_chunk_index(const GridInput *grid, GridSlot slot, i32 chunk_index) {
  grid->slots[_slot_index(slot)] = chunk_index;
}
