// chunk_internal.h
#pragma once

#include "chunk.h" // brings ChunkTree forward decl
#include "common.h"
#include "shaders/raytrace.glsl"
#include "vector.h"
#include "vox_loader.h"
#include <stdbool.h>
#include <stdint.h>
// ----- internal constants -----

typedef struct CmdBuffer CmdBuffer;

typedef uint32_t u32;
typedef uint16_t u16;
typedef uint64_t u64;

typedef enum { NODE_EMPTY = 0, NODE_FULL = 1, NODE_MIXED = 2 } NodeState;

typedef struct Node {
  u64 mask;
} Node;
typedef struct ChildIndex {
  u32 first_child_index;
} ChildIndex;

struct ChunkTree {
  // Source
  u64 bits[(CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE + 63) / 64];
  u16 vox_mat[CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE];

  // Derived products
  Vector nodes;         // Node[]
  Vector child_indices; // ChildIndex[]
  Vector palette;       // u32 RGBA[]

  // Dirty tracking
  bool is_dirty;
  bool need_upload;
  u32 pending_edits;

  // GPU handles (keep opaque here if you want to avoid GPU includes)
  ChunkGpuResources res;
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
} ChunkBuildOutput;

typedef enum {
  CHUNK_BUILD_OK = 0,
  CHUNK_BUILD_ERR_OOM,
  CHUNK_BUILD_ERR_BAD_CONFIG,
} ChunkBuildResult;

typedef struct {
  int x, y, z;
  u16 mat;
} ChunkVoxelEdit;

typedef enum {
  CHUNK_VOX_IMPORT_OK = 0,
  CHUNK_VOX_IMPORT_ERR_IO,
  CHUNK_VOX_IMPORT_ERR_OOM,
} ChunkVoxImportResult;

// --- GPU upload interface (Input -> Output) ---
typedef struct {
  const void *node_bytes;
  u32 node_size;
  const void *child_bytes;
  u32 child_size;
  const void *pal_bytes;
  u32 pal_size;
} ChunkGpuUploadInput;

typedef struct {
  u32 gpu_node;
  u32 gpu_child;
  u32 gpu_palette;
} ChunkGpuUploadOutput;

typedef struct ChunkGpuCreateDesc {
  // Minimum capacities; you can pass exact sizes or a growth policy.
  size_t node_capacity_bytes;
  size_t child_capacity_bytes;
  size_t palette_capacity_bytes;
} ChunkGpuCreateDesc;

typedef enum ChunkGpuResult {
  CHUNK_GPU_OK = 0,
  CHUNK_GPU_ERR_CREATE,
  CHUNK_GPU_ERR_UPLOAD,
} ChunkGpuResult;

typedef struct ChunkBuildScratch {
  // If provided, builder uses this as a bump allocator for temporary memory.
  // If NULL or size==0, builder falls back to calloc/free internally (Option A).
  void *mem;
  size_t size;
  size_t offset;

  // Optional: counters for debugging/profiling
  size_t peak_offset;
} ChunkBuildScratch;

// PUBLIC FUNCTIONS

void _resource_init(ChunkTree *chunk, M_Resource *rm);
void _upload_chunk(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);

// VOXEL EDITS
bool chunk_in_bounds(int v);
void chunk_fill_random(ChunkTree *chunk, u32 seed, float density);
bool chunk_import_vox_file(ChunkTree *chunk, const char *path, VoxAxisPreset vox_flags, bool center_in_chunk);

ChunkBuildResult _build_chunk(const ChunkBuildInput *in, ChunkBuildScratch *scratch, ChunkBuildOutput *out);
void _build_chunk_free(ChunkBuildOutput *out);

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
