#include <cglm/types.h> // vec3

#include "common.h"

typedef struct TransferQueue TransferQueue;
typedef struct World World;

typedef struct WorldConfig {
  vec3 min_corner; // origin for world->chunk coord mapping
  u32 chunk_size;  // size in world units (e.g. CHUNK_SIZE)
  u32 visibility;  // V: window is VxVxV
  u32 max_cached;  // cache budget; 0 means no cache limit
} WorldConfig;

// PUBLIC FUNCTIONS
void world_cpu_tick(World *w, vec3 player_pos);
void world_gpu_tick(World *w, M_Resource *rm, TransferQueue *transfer);
World *world_create(const WorldConfig *cfg, vec3 player_pos);
void world_destroy(World *w);
void world_grid_get_min_corner(World *w, vec3 out_min_corner);
i32 world_grid_get_push_id(World *w, M_Resource *rm);
void world_init_gpu(World *w, M_Resource *rm, TransferQueue *transfer);
// END PUBLIC FUNCTIONS
