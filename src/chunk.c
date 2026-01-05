
/* chunk.c */
#include "chunk.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
  int x, y, z;
} Point;

// --- Private Prototypes ---
static uint64_t split_by_3(uint32_t a);
static uint64_t morton_encode(int x, int y, int z);
static bool traverse_svo(const ChunkTree *chunk, int x, int y, int z);
static inline bool in_bounds(int v);

// -------------------- Public API --------------------
void chunk_init(ChunkTree *chunk) {
  memset(chunk, 0, sizeof(*chunk));
  vec_init(&chunk->nodes, sizeof(Node), NULL);
  vec_init(&chunk->child_indices, sizeof(ChildIndex), NULL);
  // bits[] already zero from memset
}

void chunk_destroy(ChunkTree *chunk) {
  if (chunk->nodes.data)
    free(chunk->nodes.data);
  if (chunk->child_indices.data)
    free(chunk->child_indices.data);
  memset(chunk, 0, sizeof(*chunk));
}

bool chunk_get_voxel(const ChunkTree *chunk, int x, int y, int z) {
  if (!in_bounds(x) || !in_bounds(y) || !in_bounds(z))
    return false;

  uint64_t code = morton_encode(x, y, z);
  uint64_t w = BITSET_WORD(code);
  uint32_t b = BITSET_BIT(code);
  return (chunk->bits[w] & BIT_MASK_U64(b)) != 0ull;
}

void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  if (!in_bounds(x) || !in_bounds(y) || !in_bounds(z))
    return;

  uint64_t code = morton_encode(x, y, z);
  uint64_t w = BITSET_WORD(code);
  uint32_t b = BITSET_BIT(code);
  uint64_t m = BIT_MASK_U64(b);

  uint64_t before = chunk->bits[w];
  uint64_t after = set_active ? (before | m) : (before & ~m);

  if (before != after) {
    chunk->bits[w] = after;
    chunk->is_dirty = true;
    chunk->pending_edits++;
  }
}

void chunk_rebuild_if_needed(ChunkTree *chunk, uint32_t threshold) {
  if (!chunk->is_dirty)
    return;
  if (chunk->pending_edits < threshold)
    return;
  chunk_rebuild(chunk);
  chunk->pending_edits = 0;
}

void chunk_rebuild(ChunkTree *chunk) {
  if (!chunk->is_dirty)
    return;

  vec_clear(&chunk->nodes);
  vec_clear(&chunk->child_indices);

  // Level 0: WORDS_PER_CHUNK leaf masks (each is exactly chunk->bits[i])
  // Level 1: WORDS_PER_CHUNK/64 parent masks
  // ...
  // Level (TREE_LEVELS-1): root mask count = 1
  //
  // We build a bottom-up dense mask pyramid, then flatten sparsely in BFS order.

  uint32_t level_count = (uint32_t)TREE_LEVELS;

  uint64_t *level_masks[TREE_LEVELS];
  uint32_t level_node_count[TREE_LEVELS];

  // allocate per-level dense arrays
  uint64_t current = (uint64_t)WORDS_PER_CHUNK; // leaf "node" count
  for (uint32_t d = 0; d < level_count; d++) {
    level_node_count[d] = (uint32_t)current;
    level_masks[d] = (uint64_t *)calloc(1, (size_t)current * sizeof(uint64_t));
    memset(level_masks[d], 0, (size_t)current * sizeof(uint64_t));

    // next parent level groups 64 children into 1 parent
    current = (current + 63ull) / 64ull; // ceil divide to be safe
    if (current == 0)
      current = 1;
  }

  // fill leaves from bits
  for (uint32_t i = 0; i < level_node_count[0]; i++) {
    level_masks[0][i] = chunk->bits[i];
  }

  // build parents: bit c set if child mask non-zero
  for (uint32_t d = 1; d < level_count; d++) {
    uint32_t child_count = level_node_count[d - 1];
    uint32_t parent_count = level_node_count[d];

    for (uint32_t p = 0; p < parent_count; p++) {
      uint64_t mask = 0;
      uint32_t base = p * 64u;

      uint32_t limit = 64u;
      if (base + limit > child_count)
        limit = child_count - base;

      for (uint32_t c = 0; c < limit; c++) {
        if (level_masks[d - 1][base + c] != 0ull) {
          mask |= (1ull << c);
        }
      }
      level_masks[d][p] = mask;
    }
  }

  // flatten BFS from top level down
  uint32_t level_start[TREE_LEVELS] = {0};
  uint32_t active_count[TREE_LEVELS] = {0};

  uint32_t total_active = 0;
  for (int d = (int)level_count - 1; d >= 0; d--) {
    level_start[d] = total_active;

    uint32_t count = 0;
    for (uint32_t i = 0; i < level_node_count[d]; i++) {
      if (level_masks[d][i] != 0ull)
        count++;
    }

    // always keep root as node 0
    if (d == (int)level_count - 1 && count == 0)
      count = 1;

    active_count[d] = count;
    total_active += count;
  }

  vec_realloc_capacity(&chunk->nodes, total_active);
  vec_realloc_capacity(&chunk->child_indices, total_active);

  for (int d = (int)level_count - 1; d >= 0; d--) {
    uint32_t next_level_ptr = (d > 0) ? level_start[d - 1] : 0;
    bool root_forced = (d == (int)level_count - 1);

    for (uint32_t i = 0; i < level_node_count[d]; i++) {
      uint64_t mask = level_masks[d][i];

      if (mask == 0ull && !root_forced)
        continue;

      Node n = {.mask = mask};
      ChildIndex c = {.first_child_index = (d > 0 && mask != 0ull) ? next_level_ptr : 0};

      vec_push(&chunk->nodes, &n);
      vec_push(&chunk->child_indices, &c);

      if (d > 0) {
        next_level_ptr += (uint32_t)__builtin_popcountll(mask);
      }

      if (root_forced) {
        // if we forced an empty root, only emit one node
        if (mask == 0ull)
          break;
        root_forced = false;
      }
    }
  }

  for (uint32_t d = 0; d < level_count; d++) {
    free(level_masks[d]);
  }

  chunk->is_dirty = false;
  chunk->need_upload = true;
}

void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd) {
  if (!chunk->need_upload)
    return;

  cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_node, chunk->nodes.data, vec_bytes_len(&chunk->nodes));

  cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_child_indices, chunk->child_indices.data,
                    vec_bytes_len(&chunk->child_indices));

  chunk->need_upload = false;
}

// -------------------- Tests --------------------

int chunk_test(void) {
  ChunkTree chunk;
  chunk_init(&chunk);

  LOG_INFO("Chunk Test: TREE_LEVELS=%d, CHUNK_SIZE=%u, WORDS_PER_CHUNK=%llu, MORTON_BITS=%u\n", (int)TREE_LEVELS,
           (unsigned)CHUNK_SIZE, (unsigned long long)WORDS_PER_CHUNK, (unsigned)MORTON_BITS);

  // Test 1: single voxel
  {
    LOG_INFO("[Test 1] Single voxel... ");
    memset(chunk.bits, 0, sizeof(chunk.bits));
    chunk.is_dirty = true;

    int tx = (int)(CHUNK_SIZE / 2u);
    int ty = (int)(CHUNK_SIZE / 2u);
    int tz = (int)(CHUNK_SIZE / 2u);

    chunk_set_voxel(&chunk, tx, ty, tz, true);
    chunk_rebuild(&chunk);

    bool found = traverse_svo(&chunk, tx, ty, tz);
    bool not_found = traverse_svo(&chunk, 0, 0, 0);

    if (found && !not_found)
      LOG_INFO("PASSED\n");
    else {
      LOG_INFO("FAILED (found=%d falsepos=%d)\n", (int)found, (int)not_found);
      chunk_destroy(&chunk);
      return 1;
    }
  }

  // Test 2: random sparse set
  {
    LOG_INFO("[Test 2] Random cloud (200 voxels)... ");
    memset(chunk.bits, 0, sizeof(chunk.bits));
    vec_clear(&chunk.nodes);
    vec_clear(&chunk.child_indices);
    chunk.is_dirty = true;
    chunk.pending_edits = 0;

    Point pts[200];
    unsigned int seed = 12345u;

    for (int i = 0; i < 200; i++) {
      seed = seed * 1103515245u + 12345u;

      int x = (int)((seed >> 16) & (CHUNK_SIZE - 1u));
      int y = (int)((seed >> 8) & (CHUNK_SIZE - 1u));
      int z = (int)((seed) & (CHUNK_SIZE - 1u));

      pts[i] = (Point){x, y, z};
      chunk_set_voxel(&chunk, x, y, z, true);
    }

    chunk_rebuild(&chunk);

    bool ok = true;
    for (int i = 0; i < 200; i++) {
      if (!traverse_svo(&chunk, pts[i].x, pts[i].y, pts[i].z)) {
        LOG_INFO("FAILED at (%d,%d,%d)\n", pts[i].x, pts[i].y, pts[i].z);
        ok = false;
        break;
      }
    }

    if (ok)
      LOG_INFO("PASSED (nodes=%zu)\n", chunk.nodes.length);
    else {
      chunk_destroy(&chunk);
      return 1;
    }
  }

  // Test 3: full chunk
  {
    LOG_INFO("[Test 3] Full solid chunk... ");
    memset(chunk.bits, 0xFF, sizeof(chunk.bits));
    chunk.is_dirty = true;

    chunk_rebuild(&chunk);

    // Expected nodes when fully solid and TREE_LEVELS fixed:
    // Level0: WORDS_PER_CHUNK
    // Level1: WORDS_PER_CHUNK/64
    // ...
    // Root: 1
    size_t expected = 0;
    uint64_t layer = (uint64_t)WORDS_PER_CHUNK;

    for (uint32_t i = 0; i < (uint32_t)TREE_LEVELS; i++) {
      expected += (size_t)layer;
      layer = (layer + 63ull) / 64ull;
      if (layer == 0)
        layer = 1;
    }

    if (chunk.nodes.length == expected) {
      LOG_INFO("PASSED (expected=%zu)\n", expected);
    } else {
      LOG_INFO("FAILED (expected=%zu got=%zu)\n", expected, chunk.nodes.length);
      chunk_destroy(&chunk);
      return 1;
    }
  }

  chunk_destroy(&chunk);
  LOG_INFO("All chunk tests passed.\n");
  return 0;
}
// --- Private Functions ---

// -------------------- Morton encoding --------------------
// This encoder interleaves bits as: x at bit 0, y at bit 1, z at bit 2, repeating.
// With CHUNK_SIZE=2^(2L), we need BITS_PER_AXIS = 2L bits per axis.
// For TREE_LEVELS up to 6, BITS_PER_AXIS <= 12; this is fine.
static uint64_t split_by_3(uint32_t a) {
  // Keep enough bits; 21 is plenty for our use-case.
  uint64_t x = (uint64_t)(a & 0x1FFFFFu);

  x = (x | (x << 32)) & 0x1F00000000FFFFull;
  x = (x | (x << 16)) & 0x1F0000FF0000FFull;
  x = (x | (x << 8)) & 0x100F00F00F00F00Full;
  x = (x | (x << 4)) & 0x10C30C30C30C30C3ull;
  x = (x | (x << 2)) & 0x1249249249249249ull;

  return x;
}

static uint64_t morton_encode(int x, int y, int z) {
  // We assume inputs are already clamped to [0..CHUNK_SIZE-1]
  return split_by_3((uint32_t)x) | (split_by_3((uint32_t)y) << 1) | (split_by_3((uint32_t)z) << 2);
}

// -------------------- Internal traversal for tests --------------------
static bool traverse_svo(const ChunkTree *chunk, int x, int y, int z) {
  if (chunk->nodes.length == 0)
    return false;

  uint64_t code = morton_encode(x, y, z);
  uint32_t node_index = 0;

  const Node *node_arr = (const Node *)chunk->nodes.data;
  const ChildIndex *child_arr = (const ChildIndex *)chunk->child_indices.data;

  // Traverse from root (TREE_LEVELS-1) down to 0
  for (int d = (int)TREE_LEVELS - 1; d >= 0; d--) {
    Node n = node_arr[node_index];

    uint32_t slot = CHILD_SLOT(code, (uint32_t)d);
    uint64_t bit = 1ull << slot;

    if ((n.mask & bit) == 0ull)
      return false;

    if (d == 0)
      return true; // leaf bit is the voxel

    uint64_t prefix = n.mask & (bit - 1ull);
    uint32_t offset = (uint32_t)__builtin_popcountll(prefix);

    node_index = child_arr[node_index].first_child_index + offset;
  }

  return false;
}

static inline bool in_bounds(int v) { return (v >= 0) && (v < (int)CHUNK_SIZE); }
