#include "cglm/types.h"
#include "chunk.h"
#include <stdint.h>
#include <vector.h>

// -----------------------------------------------------------------------------
// CONSTANTS & CONFIGURATION
// -----------------------------------------------------------------------------

// The "Active Window" around the player.
// A 16x16x16 grid means we track 4,096 chunks total.
// This acts as a Ring Buffer (Toroidal).
#define MAP_DIM 16

typedef struct ChunkSlot {
  ChunkTree tree;   // The actual voxel data and SVO logic
  ivec3 global_pos; // Current world position (e.g., 64, 0, -128)
  bool is_active;   // Is this slot currently used?
} ChunkSlot;

typedef struct WorldManager {
  // A 3D array of slots: [MAP_DIM][MAP_DIM][MAP_DIM]
  ChunkSlot *chunks;

  // Total size of the world in voxels (e.g., 16 * 64 = 1024)
  int world_voxel_dim;
} WorldManager;
// Maps any global voxel coordinate to the correct Chunk Index in the ring
// buffer

int get_chunk_index(int gx, int gy, int gz) {
  // 1. Convert voxel to chunk-space
  int cx = (gx / CHUNK_SIZE);
  int cy = (gy / CHUNK_SIZE);
  int cz = (gz / CHUNK_SIZE);

  // 2. Wrap using modulo for toroidal effect
  // We add MAP_DIM to handle negative coordinates correctly in C
  int lx = (cx % MAP_DIM + MAP_DIM) % MAP_DIM;
  int ly = (cy % MAP_DIM + MAP_DIM) % MAP_DIM;
  int lz = (cz % MAP_DIM + MAP_DIM) % MAP_DIM;

  // 3. Flatten to 1D array index
  return lx + (ly * MAP_DIM) + (lz * MAP_DIM * MAP_DIM);
}

void map_insert_voxel(WorldManager *world, int x, int y, int z, bool active) {
  // 1. Find which chunk this voxel belongs to
  int slot_idx = get_chunk_index(x, y, z);
  ChunkSlot *slot = &world->chunks[slot_idx];

  // 2. Find local voxel coords inside that chunk (0-63)
  int lx = (x % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
  int ly = (y % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;
  int lz = (z % CHUNK_SIZE + CHUNK_SIZE) % CHUNK_SIZE;

  // 3. Update the bits inside the ChunkTree
  chunk_set_voxel(&slot->tree, lx, ly, lz, active);
}
