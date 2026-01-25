#include "common.h"
#include <cglm/cglm.h>
#include <math.h>
#include <vector.h>

typedef struct GridInput {
  ivec3 min_corner;
  i32 *slots; // active_nodes indices
} GridInput;

typedef ivec3 GridSlot;

typedef struct GridResult {
  Vector load_coords;   // ivec3[]  (global chunk coords to load)
  Vector load_slots;    // u32[]    (slot indices for those coords), need to be assigned
  Vector unload_coords; // ivec3[]  (global chunk coords to unload/cache)
} GridResult;

static inline void vec3_to_ivec3_floor(vec3 v, ivec3 out) {
  out[0] = (int)floorf(v[0]);
  out[1] = (int)floorf(v[1]);
  out[2] = (int)floorf(v[2]);
}

// PUBLIC FUNCTIONS
i32 grid_get_chunk_index(const GridInput grid, GridSlot slot);
void grid_init(GridInput *grid, const ivec3 player_chunk, GridResult *result);
void grid_step(GridInput input, ivec3 dir, GridResult *result);
void grid_world_coord_to_chunk_pos(const ivec3 min_corner, ivec3 player_pos, ivec3 out_chunk);
// END PUBLIC FUNCTIONS
