#pragma once
#include "common.h"
#define CHUNK_SIZE 64
#define WORDS_PER_CHUNK 4096
#include "command.h"
#include "resmanager.h"
#include "util.h"
#include "vector.h"

typedef struct ChunkTree ChunkTree;

typedef struct ChunkTree {
  // flag, need to be rebuilt:true
  bool is_dirty;
  // need to update the nodes;
  bool need_upload;

  Vector nodes;
  Vector child_indices;

  ResHandle gpu_node;
  ResHandle gpu_child_indices;

  // The raw data stored in Morton Order.
  // bits[0] holds voxels 0-63 (Morton indices), bits[1] holds 64-127, etc.
  uint64_t bits[WORDS_PER_CHUNK];
} ChunkTree;

// PUBLIC FUNCTIONS

int chunk_test();
void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);
void chunk_rebuild(ChunkTree *chunk, int max_depth);
bool chunk_get_voxel(ChunkTree *chunk, int x, int y, int z);
void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active);
