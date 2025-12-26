#include "chunk.h"
#include "stdint.h"
#include "vector.h"
#include <stdint.h>

#define MAX_NODES_L1 512  // Est. max internal nodes
#define MAX_NODES_L2 4096 // Est. max leaf nodes

typedef struct Node {
  u64 mask;
} Node;

typedef struct ChildIndex {
  u32 first_child_index;
} ChildIndex;

static uint64_t split_by_3(uint32_t a) {
  u64 x = a & 0x1FFFFF; // We only care about lower 21 bits (enough for
                        // coords up to ~2 million)

  // Magic shifts to spread the bits out
  x = (x | x << 32) & 0x1F00000000FFFF;
  x = (x | x << 16) & 0x1F0000FF0000FF;
  x = (x | x << 8) & 0x100F00F00F00F00F;
  x = (x | x << 4) & 0x10C30C30C30C30C3;
  x = (x | x << 2) & 0x1249249249249249;

  return x;
}

void chunk_rebuild(ChunkTree *chunk) {
  if (!chunk->is_dirty)
    return;

  // Level 2 (Leaves): Holds the masks from chunk->bits
  u64 l2_masks[MAX_NODES_L2];
  u32 l2_count = 0;

  // Level 1 (Middle): Holds masks pointing to L2
  u64 l1_masks[MAX_NODES_L1];
  u32 l1_count = 0;

  // Root: Just one mask
  u64 root_mask = 0;

  // ---------------------------------------------------------
  // STEP 1: BOTTOM-UP SCAN
  // ---------------------------------------------------------
  // We scan the bits array.
  // The bits array is 4096 items long.
  // Level 1 groups them into blocks of 64. (4096 / 64 = 64 blocks).

  for (int i = 0; i < 64; i++) {
    u64 current_l1_mask = 0;
    u32 start_of_leaves = l2_count; // Current write head of L2 buffer
    bool has_active_leaves = false;

    // Check the 64 leaves belonging to this L1 block
    int base_bit_index = i * 64;

    for (int j = 0; j < 64; j++) {
      uint64_t leaf_val = chunk->bits[base_bit_index + j];

      if (leaf_val != 0) {
        // Found an active leaf!
        l2_masks[l2_count++] = leaf_val; // Save leaf data
        current_l1_mask |= (1ULL << j);  // Mark bit in L1 parent
        has_active_leaves = true;
      }
    }

    // If this block had active leaves, save the Level 1 node
    if (has_active_leaves) {
      root_mask |= (1ULL << i); // Mark bit in Root
      l1_masks[l1_count] = current_l1_mask;
      l1_count++;
    }
  }

  // ---------------------------------------------------------
  // STEP 2: FLATTEN (BFS Order: Root -> L1 -> L2)
  // ---------------------------------------------------------

  Node root_node = {.mask = root_mask};
  ChildIndex root_child = {.first_child_index =
                               1}; // L1 always starts at index 1

  vec_realloc_capacity(&chunk->nodes, l1_count + l2_count + 1);
  vec_realloc_capacity(&chunk->child_indices, l1_count + l2_count + 1);

  // -- B. Write Level 1 --

  u32 current_child_accumulator = 0;
  u32 l2_start_offset = 1 + l1_count;

  for (int i = 0; i < l1_count; i++) {
    Node n = {.mask = l1_masks[i]};

    // 1. Point to the current write head
    u32 child_idx = l2_start_offset + current_child_accumulator;

    ChildIndex c = {.first_child_index = child_idx};

    vec_push(&chunk->nodes, &n);
    vec_push(&chunk->child_indices, &c);

    current_child_accumulator += __builtin_popcountll(l1_masks[i]);
  }

  // -- C. Write Level 2 (Leaves) --
  for (int i = 0; i < l2_count; i++) {
    Node n = {.mask = l2_masks[i]};
    ChildIndex c = {.first_child_index =
                        0}; // Leaves have no children (or 0 to indicate end)

    vec_push(&chunk->nodes, &n);
    vec_push(&chunk->child_indices, &c);
  }

  chunk->is_dirty = false;
}

// MORTON ENCODER
// Input: x, y, z inside the chunk (Range 0 to 63)
// Output: 64-bit sortable code
// -----------------------------------------------------------------------------
static uint64_t get_morton_code(int x, int y, int z) {
  // Interleave: Z gets shifted by 2, Y by 1, X by 0
  return split_by_3(x) | (split_by_3(y) << 1) | (split_by_3(z) << 2);
}

void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  u64 code = get_morton_code(x, y, z);

  auto word_index = code >> 6;
  chunk->is_dirty |= chunk->bits[word_index] ^ (uint64_t)set_active;
  auto bit_mask = 1ULL << (code & 63);

  chunk->bits[word_index] =
      (chunk->bits[word_index] & ~bit_mask) | (set_active ? bit_mask : 0);
}

bool chunk_get_voxel(ChunkTree *chunk, int x, int y, int z) {
  uint64_t code = get_morton_code(x, y, z);
  uint64_t word_index = code >> 6;
  uint64_t bit_mask = 1ull << (code & 63);

  return (chunk->bits[word_index] & bit_mask) != 0;
}
