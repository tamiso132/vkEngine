
// chunk_internal.h (CPU-only internals)
#pragma once

#include "common.h"
#include "res_async.h"
#include "vector.h"
#include <stdbool.h>
#include <stdint.h>

// ----- internal constants/types -----

typedef struct ChunkTree ChunkTree;

#define CHUNK_SIZE 64

typedef enum { NODE_EMPTY = 0, NODE_FULL = 1, NODE_MIXED = 2 } NodeState;

typedef enum ChunkResType {
  CHUNK_RES_NODES = 0,
  CHUNK_RES_CHILDREN,
  CHUNK_RES_PALETTE,
  CHUNK_RES_LEAF_MATS,
  CHUNK_RES__COUNT,
} ChunkResType;

typedef enum ChunkResTypeBits {
  CHUNK_RES_BITMASK_NODES = 1 << CHUNK_RES_NODES,
  CHUNK_RES_BITMASK_CHILDREN = 1 << CHUNK_RES_CHILDREN,
  CHUNK_RES_BITMASK_PALETTE = 1 << CHUNK_RES_PALETTE,
  CHUNK_RES_BITMASK_LEAF_MATS = 1 << CHUNK_RES_LEAF_MATS,
} ChunkResTypeBits;

typedef struct Node {
  u64 mask;
} Node;

typedef struct ChildIndex {
  u32 first_child_index;
} ChildIndex;

struct ChunkTree {
  u64 bits[(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE + 63) / 64];
  u16 vox_mat[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];

  // Node[]
  // ChildIndex[]
  // u32 RGBA[]
  // u16 materials[]
  Vector p_res_data[CHUNK_RES__COUNT];

  ChunkResTypeBits pending_bits;
  bool is_dirty;
  u64 build_version;
};

// --- Build interface (Input -> Output) ---
typedef struct {
  const u64 *bits;
  const u16 *vox_mat;
  u32 chunk_size;
  u32 tree_levels;
} ChunkBuildInput;

typedef struct {
  Vector *nodes;         // Node[]
  Vector *child_indices; // ChildIndex[]
  Vector *leaf_mats;     // u16[]
} ChunkBuildOutput;

typedef struct ChunkBuildScratch {
  void *mem;
  size_t size;
  size_t offset;
  size_t peak_offset;
} ChunkBuildScratch;

typedef enum {
  CHUNK_BUILD_OK = 0,
  CHUNK_BUILD_ERR_OOM,
  CHUNK_BUILD_ERR_BAD_CONFIG,
} ChunkBuildResult;

// PUBLIC FUNCTIONS

ChunkBuildResult _build_chunk(const ChunkBuildInput *in, ChunkBuildScratch *scratch, ChunkBuildOutput *out);

u32 _edit_add_color(ChunkTree *chunk, u32 rgba);
bool _edit_get_voxel(const ChunkTree *chunk, int x, int y, int z);
void _edit_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active);
void _edit_set_voxel_color(ChunkTree *chunk, int x, int y, int z, bool on, u16 mat);
u32 _edit_add_color(ChunkTree *chunk, u32 rgba);
bool chunk_in_bounds(int v);
void _upload_chunk(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);

// END PUBLIC FUNCTIONS

// --- Helpers ---
inline static u32 voxel_linear_index_u32(int x, int y, int z) {
  return (u32)x + (u32)CHUNK_SIZE * ((u32)y + (u32)CHUNK_SIZE * (u32)z);
}

// idx in an NxNxN grid
inline static u32 idx3_linear_u32(u32 x, u32 y, u32 z, u32 N) { return x + N * (y + N * z); }

inline static void idx_to_xyz_u32(u32 idx, u32 N, u32 *x, u32 *y, u32 *z) {
  *x = idx % N;
  *y = (idx / N) % N;
  *z = idx / (N * N);
}

// matches shader slot packing: x | (y<<2) | (z<<4)
inline static u32 slot_linear_4x4x4(u32 lx, u32 ly, u32 lz) {
  return (lx & 3u) | ((ly & 3u) << 2u) | ((lz & 3u) << 4u);
}

// -------------------- RNG (C) --------------------
inline static u32 xorshift32(u32 *state) {
  u32 x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}
