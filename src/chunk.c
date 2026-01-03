#include "chunk.h"
#include "command.h"
#include "common.h"
#include "gpu/gpu.h"
#include "resmanager.h"
#include "stdint.h"
#include "vector.h"
#include <stdint.h>

#define LEAF_MASK 0x80000000

typedef struct Node {
  u64 mask;
} Node;

typedef struct ChildIndex {
  u32 first_child_index;
} ChildIndex;

typedef struct {
  int x, y, z;
} Point;

// Max depth 6 = 4096^3 voxels
#define MAX_TREE_DEPTH 6
#define TEST_DEPTH 4

// --- Private Prototypes ---
static uint64_t split_by_3(uint32_t a);
static uint64_t get_morton_code(int x, int y, int z);
static bool traverse_svo(ChunkTree *chunk, int x, int y, int z, int max_depth);

void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd) {
  if (chunk->need_upload)
    cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_node, chunk->nodes.data, vec_bytes_len(&chunk->nodes));
  chunk->need_upload = false;
}

void chunk_rebuild(ChunkTree *chunk, int max_depth) {
  if (!chunk->is_dirty)
    return;

  vec_clear(&chunk->nodes);
  vec_clear(&chunk->child_indices);

  // 1. Temporary buffers for each level
  // We store masks and a "popcount" for children to calculate pointers later
  uint64_t *level_masks[MAX_TREE_DEPTH];
  uint32_t level_node_count[MAX_TREE_DEPTH] = {0};

  // Initialize Level 0 (Leaves) from the bits array
  // Level 0 always has WORDS_PER_CHUNK potential nodes
  uint32_t current_level_size = WORDS_PER_CHUNK;

  // Scratchpads for structural building
  // In a real app, use a pre-allocated scratch buffer
  for (int i = 0; i < max_depth; i++) {
    level_masks[i] = calloc(1, current_level_size * sizeof(uint64_t));
    memset(level_masks[i], 0, current_level_size * sizeof(uint64_t));
    current_level_size /= 64;
    if (current_level_size < 1)
      current_level_size = 1;
  }

  // 2. STEP 1: Bottom-Up structural pass
  // Pass 0: Fill Leaf Level from raw bits
  for (int i = 0; i < WORDS_PER_CHUNK; i++) {
    level_masks[0][i] = chunk->bits[i];
  }
  level_node_count[0] = WORDS_PER_CHUNK;

  // Pass 1 to N: Build parents from children
  for (int d = 1; d < max_depth; d++) {
    int child_count = level_node_count[d - 1];
    int parent_count = child_count / 64;

    for (int p = 0; p < parent_count; p++) {
      uint64_t mask = 0;
      for (int c = 0; c < 64; c++) {
        if (level_masks[d - 1][p * 64 + c] != 0) {
          mask |= (1ULL << c);
        }
      }
      level_masks[d][p] = mask;
    }
    level_node_count[d] = parent_count;
  }

  // 3. STEP 2: Flattening (BFS Order)
  // We only push nodes that have a non-zero mask (Sparse)
  // We work from top (max_depth - 1) down to 0

  uint32_t level_start_offsets[MAX_TREE_DEPTH];
  uint32_t active_node_counts[MAX_TREE_DEPTH] = {0};

  // Calculate how many active nodes exist per level to find offsets
  uint32_t total_active = 0;
  for (int d = max_depth - 1; d >= 0; d--) {
    level_start_offsets[d] = total_active;
    for (int i = 0; i < level_node_count[d]; i++) {
      if (level_masks[d][i] != 0)
        active_node_counts[d]++;
    }
    total_active += active_node_counts[d];
  }

  vec_realloc_capacity(&chunk->nodes, total_active);
  vec_realloc_capacity(&chunk->child_indices, total_active);

  // Push to Vectors and resolve pointers
  for (int d = max_depth - 1; d >= 0; d--) {
    uint32_t next_level_child_ptr = (d > 0) ? level_start_offsets[d - 1] : 0;

    for (int i = 0; i < level_node_count[d]; i++) {
      uint64_t mask = level_masks[d][i];

      if (mask == 0 && d != max_depth - 1)
        continue; // Skip empty (except root)

      Node n = {.mask = mask};
      ChildIndex c = {.first_child_index = (d > 0 && mask != 0) ? next_level_child_ptr : 0};

      vec_push(&chunk->nodes, &n);
      vec_push(&chunk->child_indices, &c);

      // The next node's children will start after this node's children
      if (d > 0) {
        next_level_child_ptr += __builtin_popcountll(mask);
      }
    }
  }

  // Cleanup
  for (int i = 0; i < max_depth; i++)
    free(level_masks[i]);
  chunk->is_dirty = false;
}

void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  u64 code = get_morton_code(x, y, z);

  auto word_index = code >> 6;
  chunk->is_dirty |= chunk->bits[word_index] ^ (uint64_t)set_active;
  auto bit_mask = 1ULL << (code & 63);

  chunk->bits[word_index] = (chunk->bits[word_index] & ~bit_mask) | (set_active ? bit_mask : 0);
}

bool chunk_get_voxel(ChunkTree *chunk, int x, int y, int z) {
  uint64_t code = get_morton_code(x, y, z);
  uint64_t word_index = code >> 6;
  uint64_t bit_mask = 1ull << (code & 63);

  return (chunk->bits[word_index] & bit_mask) != 0;
}

int chunk_test() {
  // Initialization
  ChunkTree chunk;
  vec_init(&chunk.nodes, sizeof(Node), NULL);
  vec_init(&chunk.child_indices, sizeof(ChildIndex), NULL);

  LOG_INFO("Running C SVO Tests (Depth %d, Word Count %d)...\n", TEST_DEPTH, WORDS_PER_CHUNK);

  // ==========================================
  // TEST 1: Single Voxel
  // ==========================================
  {
    LOG_INFO("[Test 1] Single Voxel... ");
    // Reset
    memset(chunk.bits, 0, WORDS_PER_CHUNK * sizeof(u64));
    chunk.is_dirty = true;

    int tx = 2, ty = 2, tz = 2;
    chunk_set_voxel(&chunk, tx, ty, tz, true);
    chunk_rebuild(&chunk, TEST_DEPTH);

    bool found = traverse_svo(&chunk, tx, ty, tz, TEST_DEPTH);
    bool not_found = traverse_svo(&chunk, 0, 0, 0, TEST_DEPTH);

    if (found && !not_found)
      LOG_INFO("PASSED\n");
    else {
      LOG_INFO("FAILED (Found: %d, False Positive: %d)\n", found, not_found);
      return 1;
    }
  }

  // ==========================================
  // TEST 2: Random Sparse Cloud
  // ==========================================
  {
    LOG_INFO("[Test 2] Random Cloud (100 voxels)... ");
    memset(chunk.bits, 0, WORDS_PER_CHUNK * sizeof(u64));
    vec_clear(&chunk.nodes);
    vec_clear(&chunk.child_indices);
    chunk.is_dirty = true;

    Point points[100];
    int dim = 1 << TEST_DEPTH; // Effective dimension

    // Simple LCG for deterministic "random"
    unsigned int seed = 12345;
    for (int i = 0; i < 100; i++) {
      seed = seed * 1103515245 + 12345;
      // Map large random number to range [0, 4^DEPTH]
      // We use simple modulo on raw morton-ish space to ensure validity
      int x = (seed >> 16) % 32;
      int y = (seed >> 8) % 32;
      int z = seed % 32;

      points[i] = (Point){x, y, z};
      chunk_set_voxel(&chunk, x, y, z, true);
    }

    chunk_rebuild(&chunk, TEST_DEPTH);

    bool all_good = true;
    for (int i = 0; i < 100; i++) {
      if (!traverse_svo(&chunk, points[i].x, points[i].y, points[i].z, TEST_DEPTH)) {
        LOG_INFO("FAILED at %d %d %d\n", points[i].x, points[i].y, points[i].z);
        all_good = false;
        break;
      }
    }

    if (all_good)
      LOG_INFO("PASSED. Nodes used: %zu\n", chunk.nodes.length);
    else
      return 1;
  }

  // ==========================================
  // TEST 3: Full Chunk Stress
  // ==========================================
  {
    LOG_INFO("[Test 3] Full Solid Chunk... ");
    memset(chunk.bits, 0xFF, WORDS_PER_CHUNK * sizeof(u64));
    chunk.is_dirty = true;

    chunk_rebuild(&chunk, TEST_DEPTH);

    // Calculate expected nodes
    // Level 0 (Leaves): WORDS_PER_CHUNK
    // Level 1: WORDS_PER_CHUNK / 64
    size_t expected_nodes = 0;
    size_t layer_count = WORDS_PER_CHUNK;

    for (int i = 0; i < TEST_DEPTH; i++) {
      expected_nodes += layer_count;
      layer_count /= 64;
      if (layer_count == 0 && i < TEST_DEPTH - 1)
        layer_count = 1; // Root exists
    }

    if (chunk.nodes.length == expected_nodes) {
      LOG_INFO("PASSED (Node count: %zu)\n", expected_nodes);
    } else {
      LOG_INFO("FAILED. Expected %zu nodes, got %zu\n", expected_nodes, chunk.nodes.length);
      return 1;
    }
  }

  free(chunk.bits);
  free(chunk.nodes.data);
  free(chunk.child_indices.data);

  LOG_INFO("All Tests Passed.\n");
  return 0;
}

// --- Private Functions ---

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

// MORTON ENCODER
// Input: x, y, z inside the chunk (Range 0 to 63)
// Output: 64-bit sortable code
// -----------------------------------------------------------------------------
static uint64_t get_morton_code(int x, int y, int z) {
  // Interleave: Z gets shifted by 2, Y by 1, X by 0
  return split_by_3(x) | (split_by_3(y) << 1) | (split_by_3(z) << 2);
}

static bool traverse_svo(ChunkTree *chunk, int x, int y, int z, int max_depth) {
  if (chunk->nodes.length == 0)
    return false;

  u64 code = get_morton_code(x, y, z);
  u32 node_index = 0; // Root is always 0

  Node *node_arr = (Node *)chunk->nodes.data;
  ChildIndex *child_arr = (ChildIndex *)chunk->child_indices.data;

  // Traverse from Root (max_depth-1) down to 0
  for (int d = max_depth - 1; d >= 0; d--) {
    Node n = node_arr[node_index];

    // Which bit in the mask corresponds to the child we want?
    // Level d uses bits [d*6 ... d*6+5] of the morton code
    int shift = d * 6;
    u64 child_mask_idx = (code >> shift) & 63;
    u64 bit_check = 1ULL << child_mask_idx;

    // Is the bit set?
    if ((n.mask & bit_check) == 0) {
      return false; // Empty space
    }

    // If at leaf level (0), the bit IS the data
    if (d == 0) {
      return true;
    }

    // Calculate offset to child
    ChildIndex c = child_arr[node_index];
    u64 mask_prefix = n.mask & (bit_check - 1);
    int offset = __builtin_popcountll(mask_prefix);

    node_index = c.first_child_index + offset;
  }
  return false;
}
