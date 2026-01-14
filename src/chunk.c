
/* chunk.c */
#include "chunk.h"
#include "command.h"
#include "gpu/gpu.h"
#include "raytrace.glsl"
#include "resmanager.h"
#include "vector.h"

#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct {
  int x, y, z;
} Point;

typedef struct WorkItem {
  uint32_t dense;
  uint32_t out;
} WorkItem;

typedef enum { NODE_EMPTY = 0, NODE_FULL = 1, NODE_MIXED = 2 } NodeState;

// --- Private Prototypes ---
static uint64_t split_by_3(uint32_t a);
static uint32_t xorshift32(uint32_t *state);
static float rand01(uint32_t *state);
static uint64_t morton_encode(int x, int y, int z);
static bool traverse_svo(const ChunkTree *chunk, int x, int y, int z);
static inline bool in_bounds(int v);

// -------------------- Public API --------------------
void chunk_init(ChunkTree *chunk, M_Resource *rm, M_GPU *gpu, CmdBuffer cmd) {
  memset(chunk, 0, sizeof(*chunk));
  vec_init(&chunk->nodes, sizeof(Node), NULL);
  vec_init(&chunk->child_indices, sizeof(ChildIndex), NULL);

  chunk_fill_random(chunk, 1024, 1.0);
  chunk_rebuild(chunk);

  RGBufferInfo node_info = {.name = "NodeBuffer",
                            .capacity = vec_bytes_len(&chunk->nodes),
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo child_info = {.name = "childIndexBuffer",
                             .capacity = vec_bytes_len(&chunk->child_indices),
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  chunk->gpu_child_indices = rm_create_buffer(rm, &child_info);
  chunk->gpu_node = rm_create_buffer(rm, &node_info);

  chunk_upload(chunk, gpu, rm, cmd);
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

void chunk_fill_random(ChunkTree *chunk, uint32_t seed, float density) {

  if (density <= 0.0f)
    return;
  if (density > 1.0f)
    density = 1.0f;
  if (seed == 0)
    seed = 1;

  uint32_t rng = seed;

  for (int z = 0; z < (int)CHUNK_SIZE; ++z) {
    for (int y = 0; y < (int)CHUNK_SIZE; ++y) {
      for (int x = 0; x < (int)CHUNK_SIZE; ++x) {
        bool on = (rand01(&rng) < density);
        chunk_set_voxel(chunk, x, y, z, on);
      }
    }
  }

  // Rebuild tree after edits so GPU traversal sees it
  chunk_rebuild(chunk);
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
  NodeState *level_state[TREE_LEVELS];
  uint32_t level_node_count[TREE_LEVELS];

  // allocate per-level dense arrays
  uint64_t current = (uint64_t)WORDS_PER_CHUNK; // leaf "node" count
  for (uint32_t d = 0; d < level_count; ++d) {
    level_node_count[d] = (uint32_t)current;

    level_masks[d] = (uint64_t *)calloc((size_t)current, sizeof(uint64_t));
    level_state[d] = (NodeState *)calloc((size_t)current, sizeof(NodeState));

    // next parent groups 64 children
    current = (current + 63ull) / 64ull;
    if (current == 0)
      current = 1;
  }

  // fill leaves from bits
  for (uint32_t i = 0; i < level_node_count[0]; ++i) {
    uint64_t m = chunk->bits[i];
    level_masks[0][i] = m;

    if (m == 0ull) {
      level_state[0][i] = NODE_EMPTY;
    } else if (m == ~0ull) {
      level_state[0][i] = NODE_FULL;
    } else {
      level_state[0][i] = NODE_MIXED;
    }
  }

  // 3) Build parent states bottom-up:
  // parent EMPTY if all children EMPTY
  // parent FULL  if all children FULL
  // else MIXED
  //
  // Also build traversal masks:
  // - for MIXED parent: bit c set if child is not EMPTY
  // - for FULL parent: mask = ~0ull (all 64 subcells filled)
  // - for EMPTY parent: mask = 0
  // ------------------------------------------------------------
  for (uint32_t d = 1; d < level_count; ++d) {
    uint32_t child_count = level_node_count[d - 1];
    uint32_t parent_count = level_node_count[d];

    for (uint32_t p = 0; p < parent_count; ++p) {
      uint32_t base = p * 64u;

      uint32_t limit = 64u;
      if (base + limit > child_count)
        limit = child_count - base;

      bool all_empty = true;
      bool all_full = true;
      uint64_t mask = 0ull;

      for (uint32_t c = 0; c < limit; ++c) {
        NodeState cs = level_state[d - 1][base + c];

        if (cs != NODE_EMPTY)
          all_empty = false;
        if (cs != NODE_FULL)
          all_full = false;

        // for MIXED/internal traversal: child exists if not empty
        if (cs != NODE_EMPTY)
          mask |= (1ull << c);
      }

      NodeState ps;
      if (all_empty)
        ps = NODE_EMPTY;
      else if (all_full)
        ps = NODE_FULL;
      else
        ps = NODE_MIXED;

      level_state[d][p] = ps;

      if (ps == NODE_EMPTY) {
        level_masks[d][p] = 0ull;
      } else if (ps == NODE_FULL) {
        level_masks[d][p] = ~0ull; // "solid" leaf mask (all subcells filled)
      } else {
        level_masks[d][p] = mask; // bits indicate which children are non-empty
      }
    }
  }

  Vector curQ, nextQ;
  vec_init(&curQ, sizeof(WorkItem), NULL);
  vec_init(&nextQ, sizeof(WorkItem), NULL);

  // Emit root always (even if empty)
  {
    uint32_t top = level_count - 1;
    uint64_t rootMask = level_masks[top][0];
    Node rootN = {.mask = rootMask};
    ChildIndex rootC = {.first_child_index = 0u};

    vec_push(&chunk->nodes, &rootN);
    vec_push(&chunk->child_indices, &rootC);

    WorkItem w = {.dense = 0u, .out = 0u};
    vec_push(&curQ, &w);
  }

  for (int d = (int)level_count - 1; d >= 0; --d) {
    vec_clear(&nextQ);

    for (u32 qi = 0; qi < vec_len(&curQ); ++qi) {
      WorkItem *w = VEC_AT(&curQ, qi, WorkItem);

      uint64_t mask = level_masks[d][w->dense];
      NodeState state = level_state[d][w->dense];

      // Update output node mask (already set for root, but fine for all)
      Node *outN = VEC_AT(&chunk->nodes, w->out, Node);
      outN->mask = mask;

      ChildIndex *outCI = VEC_AT(&chunk->child_indices, w->out, ChildIndex);

      // Leaf decision:
      // - true leaf at d==0
      // - early leaf if FULL at d>0
      bool is_leaf = (d == 0) || (state == NODE_FULL);

      if (is_leaf) {
        outCI->first_child_index = LEAF_BIT; // base ignored
        continue;
      }

      // Empty node: no children
      if (state == NODE_EMPTY || mask == 0ull) {
        outCI->first_child_index = 0u;
        continue;
      }

      // MIXED internal node: emit children for each set bit in mask
      uint32_t base = (uint32_t)vec_len(&chunk->nodes);
      outCI->first_child_index = (base & INDEX_MASK); // leaf bit clear

      for (uint32_t slot = 0; slot < 64u; ++slot) {
        if (((mask >> slot) & 1ull) == 0ull)
          continue;

        uint32_t childDense = w->dense * 64u + slot;

        // Safety for last partial parent groups
        if ((uint32_t)d > 0) {
          uint32_t childCount = level_node_count[d - 1];
          if (childDense >= childCount)
            continue;
        }

        // Child might be EMPTY if you ever want to include empties; we don't.
        NodeState cs = (d > 0) ? level_state[d - 1][childDense] : NODE_MIXED;
        if (d > 0 && cs == NODE_EMPTY)
          continue;

        uint64_t childMask = (d > 0) ? level_masks[d - 1][childDense] : 0ull;

        uint32_t childOut = (uint32_t)vec_len(&chunk->nodes);

        Node cn = {.mask = childMask};
        ChildIndex cci = {.first_child_index = 0u};

        vec_push(&chunk->nodes, &cn);
        vec_push(&chunk->child_indices, &cci);

        WorkItem nw = {.dense = childDense, .out = childOut};
        vec_push(&nextQ, &nw);
      }
    }

    vec_clear(&curQ);
    for (u32 i = 0; i < vec_len(&nextQ); ++i) {
      WorkItem *it = VEC_AT(&nextQ, i, WorkItem);
      vec_push(&curQ, it);
    }

    if (d == 0)
      break;
  }

  vec_free(&curQ);
  vec_free(&nextQ);

  for (uint32_t d = 0; d < level_count; ++d) {
    free(level_masks[d]);
    free(level_state[d]);
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
  // ChunkTree chunk;
  // chunk_init(&chunk);
  //
  // LOG_INFO("Chunk Test: TREE_LEVELS=%d, CHUNK_SIZE=%u, WORDS_PER_CHUNK=%llu, MORTON_BITS=%u\n", (int)TREE_LEVELS,
  //          (unsigned)CHUNK_SIZE, (unsigned long long)WORDS_PER_CHUNK, (unsigned)MORTON_BITS);
  //
  // // Test 1: single voxel
  // {
  //   LOG_INFO("[Test 1] Single voxel... ");
  //   memset(chunk.bits, 0, sizeof(chunk.bits));
  //   chunk.is_dirty = true;
  //
  //   int tx = (int)(CHUNK_SIZE / 2u);
  //   int ty = (int)(CHUNK_SIZE / 2u);
  //   int tz = (int)(CHUNK_SIZE / 2u);
  //
  //   chunk_set_voxel(&chunk, tx, ty, tz, true);
  //   chunk_rebuild(&chunk);
  //
  //   bool found = traverse_svo(&chunk, tx, ty, tz);
  //   bool not_found = traverse_svo(&chunk, 0, 0, 0);
  //
  //   if (found && !not_found)
  //     LOG_INFO("PASSED\n");
  //   else {
  //     LOG_INFO("FAILED (found=%d falsepos=%d)\n", (int)found, (int)not_found);
  //     chunk_destroy(&chunk);
  //     return 1;
  //   }
  // }
  //
  // // Test 2: random sparse set
  // {
  //   LOG_INFO("[Test 2] Random cloud (200 voxels)... ");
  //   memset(chunk.bits, 0, sizeof(chunk.bits));
  //   vec_clear(&chunk.nodes);
  //   vec_clear(&chunk.child_indices);
  //   chunk.is_dirty = true;
  //   chunk.pending_edits = 0;
  //
  //   Point pts[200];
  //   unsigned int seed = 12345u;
  //
  //   for (int i = 0; i < 200; i++) {
  //     seed = seed * 1103515245u + 12345u;
  //
  //     int x = (int)((seed >> 16) & (CHUNK_SIZE - 1u));
  //     int y = (int)((seed >> 8) & (CHUNK_SIZE - 1u));
  //     int z = (int)((seed) & (CHUNK_SIZE - 1u));
  //
  //     pts[i] = (Point){x, y, z};
  //     chunk_set_voxel(&chunk, x, y, z, true);
  //   }
  //
  //   chunk_rebuild(&chunk);
  //
  //   bool ok = true;
  //   for (int i = 0; i < 200; i++) {
  //     if (!traverse_svo(&chunk, pts[i].x, pts[i].y, pts[i].z)) {
  //       LOG_INFO("FAILED at (%d,%d,%d)\n", pts[i].x, pts[i].y, pts[i].z);
  //       ok = false;
  //       break;
  //     }
  //   }
  //
  //   if (ok)
  //     LOG_INFO("PASSED (nodes=%zu)\n", chunk.nodes.length);
  //   else {
  //     chunk_destroy(&chunk);
  //     return 1;
  //   }
  // }
  //
  // // Test 3: full chunk
  // {
  //   LOG_INFO("[Test 3] Full solid chunk... ");
  //   memset(chunk.bits, 0xFF, sizeof(chunk.bits));
  //   chunk.is_dirty = true;
  //
  //   chunk_rebuild(&chunk);
  //
  //   // Expected nodes when fully solid and TREE_LEVELS fixed:
  //   // Level0: WORDS_PER_CHUNK
  //   // Level1: WORDS_PER_CHUNK/64
  //   // ...
  //   // Root: 1
  //   size_t expected = 0;
  //   uint64_t layer = (uint64_t)WORDS_PER_CHUNK;
  //
  //   for (uint32_t i = 0; i < (uint32_t)TREE_LEVELS; i++) {
  //     expected += (size_t)layer;
  //     layer = (layer + 63ull) / 64ull;
  //     if (layer == 0)
  //       layer = 1;
  //   }
  //
  //   if (chunk.nodes.length == expected) {
  //     LOG_INFO("PASSED (expected=%zu)\n", expected);
  //   } else {
  //     LOG_INFO("FAILED (expected=%zu got=%zu)\n", expected, chunk.nodes.length);
  //     chunk_destroy(&chunk);
  //     return 1;
  //   }
  // }
  //
  // chunk_destroy(&chunk);
  // LOG_INFO("All chunk tests passed.\n");
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

static uint32_t xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static float rand01(uint32_t *state) {
  // 24-bit fraction -> [0,1)
  return (xorshift32(state) & 0x00FFFFFFu) / 16777216.0f;
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

    uint32_t slot = CHILD_SLOT(code, d);
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
