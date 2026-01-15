// chunk_edit.h
#include "chunk_internal.h"
#include "world/chunk.h"
// chunk_edit.c

bool chunk_in_bounds(int v) { return (v >= 0) && (v < (int)CHUNK_SIZE); }

bool _edit_get_voxel(const ChunkTree *chunk, int x, int y, int z) {
  if (!chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return false;
  u32 idx = voxel_linear_index_u32(x, y, z);
  return ((chunk->bits[idx >> 6] >> (idx & 63u)) & 1ull) != 0ull;
}

void _edit_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  if (!chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return;
  u32 idx = voxel_linear_index_u32(x, y, z);
  u32 w = idx >> 6;
  u32 b = idx & 63u;
  u64 m = 1ull << b;

  u64 before = chunk->bits[w];
  u64 after = set_active ? (before | m) : (before & ~m);

  if (before != after) {
    chunk->bits[w] = after;
    chunk->is_dirty = true;
    chunk->pending_edits++;
    if (!set_active)
      chunk->vox_mat[idx] = 0;
  }
}

u32 _edit_add_color(ChunkTree *chunk, u32 rgba) { return vec_push(&chunk->palette, &rgba); }

void _edit_set_voxel_color(ChunkTree *chunk, int x, int y, int z, bool on, u16 mat) {
  if (!chunk_in_bounds(x) || !chunk_in_bounds(y) || !chunk_in_bounds(z))
    return;

  u32 vidx = voxel_linear_index_u32(x, y, z);
  u32 w = vidx >> 6;
  u32 b = vidx & 63u;
  u64 m = 1ull << b;

  u64 before = chunk->bits[w];
  u64 after = on ? (before | m) : (before & ~m);

  if (before != after) {
    chunk->bits[w] = after;
    chunk->is_dirty = true;
    chunk->pending_edits++;
  }
  chunk->vox_mat[vidx] = on ? mat : 0;
}

static float rand01(u32 *state) { return (xorshift32(state) & 0x00FFFFFFu) / 16777216.0f; }

void _edit_fill_random(ChunkTree *chunk, u32 seed, float density) {
  if (density <= 0.0f)
    return;
  if (density > 1.0f)
    density = 1.0f;
  if (seed == 0)
    seed = 1;

  u32 rng = seed;
  for (int z = 0; z < (int)CHUNK_SIZE; ++z)
    for (int y = 0; y < (int)CHUNK_SIZE; ++y)
      for (int x = 0; x < (int)CHUNK_SIZE; ++x)
        _edit_set_voxel(chunk, x, y, z, (rand01(&rng) < density));
}

void _edit_set_box(ChunkTree *chunk, int x, int y, int z, u32 palette_index, u32 size) {
  for (u32 dz = 0; dz < size; ++dz)
    for (u32 dy = 0; dy < size; ++dy)
      for (u32 dx = 0; dx < size; ++dx)
        _edit_set_voxel_color(chunk, x + (int)dx, y + (int)dy, z + (int)dz, true, palette_index);
}
