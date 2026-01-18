#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cglm/ivec3.h"
#include "cglm/types.h"
#include "cglm/vec3.h"
#include "common.h"
#include "grid.h"
#include "iterator.h"
#include "vector.h"
#include "world/chunk.h"
#include "world/chunk_gpu.h"
#include "world/storage.h"

typedef struct GPUGridSlot {
  i32 nodes_id;
  i32 child_id;
  i32 palette_id;
  i32 leaf_mats_id;
} GPUGridSlot;

struct World {
  WorldConfig cfg;
  FixedGrid *grid;
  ChunkStore chunks;
  bool is_descriptor_dirty;
};

// --- Private Prototypes ---
static void _player_pos_to_chunk_coord(const World *w, vec3 player_pos, ivec3 out_chunk);

static void _update_slots(World *w, vec3 player_pos);

static void _on_entered(const Vector *entered, void *user);
static void _on_left(const Vector *left, void *user);

World *world_create(const WorldConfig *cfg) {
  if (!cfg || cfg->visibility == 0 || cfg->chunk_size == 0)
    return NULL;

  World *w = (World *)calloc(1, sizeof(World));
  if (!w)
    return NULL;
  w->cfg = *cfg;

  // init chunk storage/residency
  if (chunk_store_init(&w->chunks, cfg->max_cached) != CHUNK_STORE_OK) {
    free(w);
    return NULL;
  }

  // create fixed grid with callbacks
  FixedGridConfig gc;
  memset(&gc, 0, sizeof(gc));
  gc.visibility = cfg->visibility;
  glm_ivec3_zero(gc.initial_center);
  gc.cb.on_entered = _on_entered;
  gc.cb.on_left = _on_left;
  gc.cb.user = w;

  if (fixed_grid_create(&w->grid, &gc) != FIXED_GRID_OK) {
    chunk_store_destroy(&w->chunks);
    free(w);
    return NULL;
  }

  // Optionally seed initial window (so store becomes populated immediately)
  fixed_grid_set_center(w->grid, gc.initial_center);

  return w;
}

void world_destroy(World *w) {
  if (!w)
    return;
  if (w->grid)
    fixed_grid_destroy(w->grid);
  chunk_store_destroy(&w->chunks);
  free(w);
}

void world_cpu_tick(World *w, vec3 player_pos) {
  if (!w)
    return;

  _update_slots(w, player_pos);

  if (w->is_descriptor_dirty) {
    IndirectIter it = chunk_store_get_active(&w->chunks);
    IITER_FOREACH(ci, ChunkItem, &it){chunk_gpu_state(const ChunkGpu *cg, ChunkResType res_type)};
  }

  // iterate active chunks
}

// --- Private Functions ---

// --- coord conversion only belongs in World (game-space -> chunk-space) ---
static void _player_pos_to_chunk_coord(const World *w, vec3 player_pos, ivec3 out_chunk) {
  vec3 rel;
  glm_vec3_sub(player_pos, (float *)w->cfg.min_corner, rel);
  out_chunk[0] = (int)floorf(rel[0] / (float)w->cfg.chunk_size);
  out_chunk[1] = (int)floorf(rel[1] / (float)w->cfg.chunk_size);
  out_chunk[2] = (int)floorf(rel[2] / (float)w->cfg.chunk_size);
}

static void _update_slots(World *w, vec3 player_pos) {
  if (!w)
    return;
  ivec3 player_chunk;
  _player_pos_to_chunk_coord(w, player_pos, player_chunk);
  fixed_grid_set_center(w->grid, player_chunk); // callbacks do the real work
}

// --- FixedGrid callbacks: just forward lists to ChunkStore ---
static void _on_entered(const Vector *entered, void *user) {
  World *w = (World *)user;
  (void)chunk_store_apply_entered(&w->chunks, entered);
  w->is_descriptor_dirty = true;
}

static void _on_left(const Vector *left, void *user) {
  World *w = (World *)user;
  (void)chunk_store_apply_left_to_cache(&w->chunks, left);
  w->is_descriptor_dirty = true;
}
