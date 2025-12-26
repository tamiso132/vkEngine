#include "cglm/types.h"
#include <stdint.h>
#include <vector.h>

// -----------------------------------------------------------------------------
// CONSTANTS & CONFIGURATION
// -----------------------------------------------------------------------------
// Why 64? It fits the 3-level Tree64 perfectly (4^3 -> 16^3 -> 64^3).
#define CHUNK_SIZE 64

// The "Active Window" around the player.
// A 16x16x16 grid means we track 4,096 chunks total.
// This acts as a Ring Buffer (Toroidal).
#define MAP_DIM 16

// Maximum nodes a single Chunk can hold.
// 4096 is usually enough for the surface of a 64^3 chunk.
// If a chunk is very complex, you might need a "next_page_index" for overflow.
#define NODES_PER_PAGE 4096

// Total number of pages in the GPU memory pool.
// Should be <= MAP_DIM^3, but can be smaller if you expect much empty air.
#define POOL_CAPACITY (MAP_DIM * MAP_DIM * MAP_DIM)

typedef struct VoxelMap {
  Vector nodes;
  Vector child_bases;
  int m_level;
  vec3 root;
} VoxelMap;

VoxelMap *map_init(int m_level) {
  VoxelMap *map = malloc(sizeof(VoxelMap));

  map->m_level = m_level;

  vec_init(&map->nodes, sizeof(Node), NULL);
  vec_init(&map->child_bases, sizeof(Node), NULL);

  return map;
}

void map_insert_voxel(VoxelMap *map, int x, int y, int z) {}

uint32_t get_root_size(int m_level) { return 1 << m_level; }

uint32_t get_relative_index(uint32_t x, uint32_t y, uint32_t z) {
  return x | y << 2 | z << 4;
}
