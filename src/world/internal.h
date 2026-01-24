#include "common.h"
#include <cglm/cglm.h>
#include <vector.h>

typedef struct GridInput {
  ivec3 min_corner;
  i32 *slots;
} GridInput;

typedef ivec3 GridSlot;

typedef struct GridResult {
  Vector load_coords;   // ivec3[]  (global chunk coords to load)
  Vector load_slots;    // u32[]    (slot indices for those coords), need to be assigned
  Vector unload_coords; // ivec3[]  (global chunk coords to unload/cache)
} GridResult;

// PUBLIC FUNCTIONS
void grid_init(GridInput *grid, const ivec3 player_chunk, GridResult *result);
void grid_step(GridInput *input, ivec3 dir, GridResult *result);
// END PUBLIC FUNCTIONS
