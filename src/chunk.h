
/* chunk.h */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "command.h"
#include "common.h"
#include "resmanager.h"
#include "util.h"
#include "vector.h"

/*
  64-tree config (4x4x4 per level):
  - 2 bits per axis per level
  - 6 morton bits per level (2*3)
  - chunk size per axis = 4^L = 2^(2L)

  Change TREE_LEVELS to scale the chunk resolution.
  NOTE: Dense bitset storage becomes huge at TREE_LEVELS>=5.
*/
#define TREE_LEVELS 3

#define BITS_PER_AXIS_PER_LEVEL 2
#define AXIS_COUNT 3
#define BITS_PER_LEVEL (BITS_PER_AXIS_PER_LEVEL * AXIS_COUNT) /* 6 */
#define BITS_PER_AXIS (BITS_PER_AXIS_PER_LEVEL * TREE_LEVELS) /* 2L */
#define CHUNK_SIZE (1u << BITS_PER_AXIS)                      /* 4^L */

#define MORTON_BITS (BITS_PER_LEVEL * TREE_LEVELS) /* 6L */

#define VOXELS_PER_WORD 64ull
#define VOXELS_PER_CHUNK (1ull << (BITS_PER_LEVEL * TREE_LEVELS)) /* 2^(6L) */
#define WORDS_PER_CHUNK (VOXELS_PER_CHUNK / VOXELS_PER_WORD)
#define BYTES_PER_CHUNK_BITSET (WORDS_PER_CHUNK * sizeof(uint64_t))

// traversal / indexing helpers
#define LEVEL_SHIFT(d) ((uint32_t)(d) * (uint32_t)BITS_PER_LEVEL)
#define CHILD_SLOT(morton, d) (uint32_t)(((morton) >> LEVEL_SHIFT(d)) & 63ull)

#define BITSET_WORD(morton) ((uint64_t)(morton) >> 6)
#define BITSET_BIT(morton) ((uint32_t)((morton) & 63ull))
#define BIT_MASK_U64(bit) (1ull << ((bit) & 63u))

_Static_assert(BITS_PER_LEVEL == 6, "64-tree requires 6 bits per level.");
_Static_assert((CHUNK_SIZE & (CHUNK_SIZE - 1u)) == 0u, "CHUNK_SIZE must be a power of two.");
_Static_assert((VOXELS_PER_CHUNK % VOXELS_PER_WORD) == 0ull, "VOXELS_PER_CHUNK must be divisible by 64.");

// PUBLIC FUNCTIONS
typedef struct Node {
  uint64_t mask; // occupancy of 64 children (or 64 voxels at leaf level)
} Node;

typedef struct ChildIndex {
  uint32_t first_child_index; // base index into the next level's compact node array
} ChildIndex;

typedef struct ChunkTree {
  bool is_dirty;
  bool need_upload;
  uint32_t pending_edits;

  Vector nodes;         // Node[]
  Vector child_indices; // ChildIndex[]

  ResHandle gpu_node;
  ResHandle gpu_child_indices;

  // Dense voxel truth table, 1 bit per voxel, in Morton order.
  uint64_t bits[WORDS_PER_CHUNK];
} ChunkTree;

// lifecycle
void chunk_init(ChunkTree *chunk);
void chunk_destroy(ChunkTree *chunk);

// voxel ops (chunk-local coordinates 0..CHUNK_SIZE-1)
bool chunk_get_voxel(const ChunkTree *chunk, int x, int y, int z);
void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active);

// rebuild & upload
void chunk_rebuild(ChunkTree *chunk);
void chunk_rebuild_if_needed(ChunkTree *chunk, uint32_t threshold);
void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);

// tests
int chunk_test(void);
