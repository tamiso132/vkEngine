#include "common.h"
#include "vector.h"

static const u32 MAX_CHUNK_VISIBILITY = 4;
static const u32 MAX_GRID_SIZE = MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY * MAX_CHUNK_VISIBILITY;

typedef struct GridSlots {
  Vector chunk_trees;        // ChunkTree[]
  struct hashmap *grid_slot; // slot to chunk_tree index
  u32 chunk_slots[MAX_GRID_SIZE];
  Vector active_chunks;
  Vector unloaded_chunks;
} GridSlots;

static const ivec3 CENTER_GRID_SLOT = {
    MAX_CHUNK_VISIBILITY / 2,
    MAX_CHUNK_VISIBILITY / 2,
    MAX_CHUNK_VISIBILITY / 2,
};
