//! chunk_internal.h
// chunk_edit.c (CPU-only)
#include "chunk_internal.h"

// --- Private Prototypes ---
bool chunk_in_bounds(int v) { return (v >= 0) && (v < (int)CHUNK_SIZE); }

bool _edit_get_voxel(const ChunkTree *chunk, int x, int y, int z) {
  if (!chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return false;
  u32 idx = voxel_linear_index_u32(x, y, z);
  return ((chunk->bits[idx >> 6] >> (idx & 63u)) & 1ull) != 0ull;
}

void _edit_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  if (!chunk || !chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return;

  u32 idx = voxel_linear_index_u32(x, y, z);
  u32 w = idx >> 6;
  u32 b = idx & 63u;
  u64 m = 1ull << b;

  u64 before = chunk->bits[w];
  u64 after = set_active ? (before | m) : (before & ~m);

  if (before != after) {
    chunk->bits[w] = after;
    if (!set_active)
      chunk->vox_mat[idx] = 0;

    chunk->pending_bits |= CHUNK_RES_BITMASK_NODES | CHUNK_RES_BITMASK_LEAF_MATS | CHUNK_RES_BITMASK_CHILDREN;
    chunk->is_dirty = true;
  }
}

u32 _edit_add_color(ChunkTree *chunk, u32 rgba) {
  chunk->pending_bits |= CHUNK_RES_BITMASK_PALETTE;
  return vec_push(&chunk->p_res_data[CHUNK_RES_PALETTE], &rgba);
}

void _edit_set_voxel_color(ChunkTree *chunk, int x, int y, int z, bool on, u16 mat) {
  if (!chunk || !chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return;

  u32 vidx = voxel_linear_index_u32(x, y, z);
  u32 w = vidx >> 6;
  u32 b = vidx & 63u;
  u64 m = 1ull << b;

  u64 before = chunk->bits[w];
  u64 after = on ? (before | m) : (before & ~m);

  if (before != after) {
    chunk->bits[w] = after;
    chunk->pending_bits |= CHUNK_RES_BITMASK_LEAF_MATS;
    chunk->is_dirty = true;
  }
  chunk->vox_mat[vidx] = on ? mat : 0;
}
// --- Private Functions ---
