#include "shaders/raytrace.glsl"
#include "vector.h"

#include "chunk.h"

#define BITS_PER_LEVEL (BITS_PER_AXIS_PER_LEVEL * AXIS_COUNT) /* 6 */
#define BITS_PER_AXIS (BITS_PER_AXIS_PER_LEVEL * TREE_LEVELS) /* 2L */

#define MORTON_BITS (BITS_PER_LEVEL * TREE_LEVELS) /* 6L */

#define WORDS_PER_CHUNK (VOXELS_PER_CHUNK / VOXELS_PER_WORD)
#define BYTES_PER_CHUNK_BITSET (WORDS_PER_CHUNK * sizeof(uint64_t))

_Static_assert(BITS_PER_LEVEL == 6, "64-tree requires 6 bits per level.");
_Static_assert((CHUNK_SIZE & (CHUNK_SIZE - 1u)) == 0u, "CHUNK_SIZE must be a power of two.");
_Static_assert((VOXELS_PER_CHUNK % VOXELS_PER_WORD) == 0ull, "VOXELS_PER_CHUNK must be divisible by 64.");


typedef enum { NODE_EMPTY = 0, NODE_FULL = 1, NODE_MIXED = 2 } NodeState;


typedef struct ChunkTree {
  bool is_dirty;
  bool need_upload;
  u32 pending_edits;

  Vector nodes;         // Node[]
  Vector child_indices; // ChildIndex[]

  Vector palette;

  ResHandle gpu_node;
  ResHandle gpu_child_indices;

  // optional: palette buffer (256 RGBA entries) if you want shader to colorize
  ResHandle gpu_palette;

  u64 bits[WORDS_PER_CHUNK];
  u16 vox_mat[VOXELS_PER_CHUNK];
} ChunkTree; 

// PUBLIC FUNCTIONS

static inline uint32_t voxel_linear_index_u32(int x, int y, int z) {
    return (uint32_t)x + (uint32_t)CHUNK_SIZE * ((uint32_t)y + (uint32_t)CHUNK_SIZE * (uint32_t)z);
}

static inline uint32_t idx3_linear_u32(uint32_t x, uint32_t y, uint32_t z, uint32_t N) {
    return x + N * (y + N * z);
}

static inline void idx_to_xyz_u32(uint32_t idx, uint32_t N, uint32_t *x, uint32_t *y, uint32_t *z) {
    *x = idx % N;
    *y = (idx / N) % N;
    *z = idx / (N * N);
}

static inline uint32_t slot_linear_4x4x4(uint32_t lx, uint32_t ly, uint32_t lz) {
    return (lx & 3u) | ((ly & 3u) << 2u) | ((lz & 3u) << 4u);
}

static inline bool in_bounds(int v) { 
    return (v >= 0) && (v < (int)CHUNK_SIZE); 
}