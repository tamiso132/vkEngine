#pragma once
#define CHUNK_SIZE 64
#define WORDS_PER_CHUNK 4096

#include "util.h"
#include "vector.h"

// PUBLIC FUNCTIONS
typedef struct ChunkTree {
  // flag, need to be rebuilt:true
  bool is_dirty;

  Vector nodes;
  Vector child_indices;

  // The raw data stored in Morton Order.
  // bits[0] holds voxels 0-63 (Morton indices), bits[1] holds 64-127, etc.
  uint64_t bits[WORDS_PER_CHUNK];
} ChunkTree;

void chunk_rebuild(ChunkTree *chunk, int max_depth);
bool chunk_get_voxel(ChunkTree *chunk, int x, int y, int z);
void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active);
