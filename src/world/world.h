#pragma once
#include "common.h"
#include <cglm/types.h> // vec3

typedef struct TransferQueue TransferQueue;
typedef struct World World;

typedef struct WorldConfig {
  vec3 min_corner; // origin for world->chunk coord mapping
  u32 chunk_size;  // size in world units (e.g. CHUNK_SIZE)
  u32 visibility;  // V: window is VxVxV
  u32 max_cached;  // cache budget; 0 means no cache limit
} WorldConfig;

// ---- lifecycle ----
World *world_create(const WorldConfig *cfg);
void world_destroy(World *w);

void world_cpu_tick(World *w, vec3 player_pos);
void world_async_transfer(World *w, TransferQueue *transfer);

// ---- convenience ----
u32 world_active_count(const World *w);
