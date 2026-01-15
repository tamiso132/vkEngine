
/* chunk.h */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "command.h"
#include "common.h"
#include "resmanager.h"
#include "util.h"
#include "vector.h"


typedef struct Node {
  uint64_t mask; // occupancy of 64 children (or 64 voxels at leaf level)
} Node;

typedef struct ChildIndex {
  uint32_t first_child_index; // base index into the next level's compact node array
} ChildIndex;

typedef struct ChunkTree ChunkTree;



void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active);
void chunk_set_voxel_color(ChunkTree *chunk, int x, int y, int z, bool on, u16 mat);

// lifecycle
void chunk_init(ChunkTree *chunk, M_Resource *rm, M_GPU *gpu, CmdBuffer cmd);
void chunk_destroy(ChunkTree *chunk);

// voxel ops (chunk-local coordinates 0..CHUNK_SIZE-1)
bool chunk_get_voxel(const ChunkTree *chunk, int x, int y, int z);

// rebuild & upload
void chunk_fill_random(ChunkTree *chunk, uint32_t seed, float density);
void chunk_rebuild(ChunkTree *chunk);
void chunk_rebuild_if_needed(ChunkTree *chunk, uint32_t threshold);
void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);

// tests
int chunk_test(void);
