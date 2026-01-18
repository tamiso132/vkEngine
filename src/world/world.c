#include "cglm/call/vec3.h"
#include "cglm/vec3-ext.h"
#include "cglm/vec3.h"
#include "common.h"
#include "hashmaputil.h"
#include "transfer_queue.h"
#include "vector.h"

#include "shaders/rt/rt_shared.glsl"
#include "world/chunk_internal.h"
#include "world/world_internal.h"

typedef struct WorldGridView {
  vec3 min_corner;
  GridSlots slots;
} WorldGridView;

WorldGridView *world_init() {
  WorldGridView *view = calloc(sizeof(WorldGridView), 1);
  view->slots.grid_slot = hm_grid_slot_new(64);
  vec_init(&view->slots.chunk_trees, sizeof(ChunkTree), NULL);
  return view;
}

void world_tick_cpu(WorldGridView *view) {
  // tick the cpu things
}

void world_async_transfer(WorldGridView *view, TransferQueue *transfer) {}

void _world_load_chunk(WorldGridView *view, vec3 chunk_world_coord) {
  // CHECK IF ALREADY EXISTED
  // LOAD IF NOT EXISTED
  // ADD TO UPLOAD LIST
  // SET FLAG
}

void _world_unload_chunk(WorldGridView *view, vec3 chunk_world_coord) {
  // CHECK IF EXIST
  // GET THE INDEX AND SET TO -1
  // TRANSFER TO A LOADED HASHMAP,
}

// --- Private Prototypes ---
static void _get_vec3_grid(WorldGridView *view, vec3 world_cordinate, vec3 out_grid_slot);

void world_update_slots(WorldGridView *view, vec3 player_pos) {
  vec3 player_grid = {};
  _get_vec3_grid(view, player_pos, player_grid);
}

// --- Private Functions ---

static void _get_vec3_grid(WorldGridView *view, vec3 world_cordinate, vec3 out_grid_slot) {

  vec3 relative_pos;
  glm_vec3_sub(world_cordinate, view->min_corner, relative_pos);
  glm_vec3_floor(relative_pos, relative_pos);
  glm_vec3_scale(relative_pos, 1.0 / CHUNK_SIZE, out_grid_slot);
}

static u32 _get_slot_grid(WorldGridView *view, vec3 world_cordinate) {
  vec3 grid_coordinate = {};
  _get_vec3_grid(view, world_cordinate, grid_coordinate);

  // x->z->y
  return grid_coordinate[0] + grid_coordinate[2] * MAX_CHUNK_VISIBILITY +
         grid_coordinate[1] * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY;
}
