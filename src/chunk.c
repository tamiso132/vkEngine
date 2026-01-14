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
  uint32_t dense; // dense index within level d grid (linear 3D)
  uint32_t out;   // compact node index in chunk->nodes/child_indices
} WorkItem;

typedef enum { NODE_EMPTY = 0, NODE_FULL = 1, NODE_MIXED = 2 } NodeState;

// --- Private Prototypes ---
static void _set_box(ChunkTree *chunk, vec3 pos, u32 size);
static inline bool in_bounds(int v);

// -------------------- RNG (C) --------------------
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

// -------------------- Linear helpers --------------------
static inline uint32_t voxel_linear_index_u32(int x, int y, int z) {
  // idx = x + N*(y + N*z)
  return (uint32_t)x + (uint32_t)CHUNK_SIZE * ((uint32_t)y + (uint32_t)CHUNK_SIZE * (uint32_t)z);
}

static inline uint32_t idx3_linear_u32(uint32_t x, uint32_t y, uint32_t z, uint32_t N) {
  // idx in an NxNxN grid
  return x + N * (y + N * z);
}

static inline void idx_to_xyz_u32(uint32_t idx, uint32_t N, uint32_t *x, uint32_t *y, uint32_t *z) {
  *x = idx % N;
  *y = (idx / N) % N;
  *z = idx / (N * N);
}

static inline uint32_t slot_linear_4x4x4(uint32_t lx, uint32_t ly, uint32_t lz) {
  // matches shader: x | (y<<2) | (z<<4)
  return (lx & 3u) | ((ly & 3u) << 2u) | ((lz & 3u) << 4u);
}

// -------------------- Public API --------------------
void chunk_init(ChunkTree *chunk, M_Resource *rm, M_GPU *gpu, CmdBuffer cmd) {
  memset(chunk, 0, sizeof(*chunk));
  vec_init(&chunk->nodes, sizeof(Node), NULL);
  vec_init(&chunk->child_indices, sizeof(ChildIndex), NULL);

  // Example content
  // _set_box(chunk, (vec3){10, 0, 10}, 10);
  chunk_set_voxel(chunk, 5, 5, 5, true);
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

  uint32_t idx = voxel_linear_index_u32(x, y, z);
  uint32_t w = idx >> 6;
  uint32_t b = idx & 63u;
  return (chunk->bits[w] & (1ull << b)) != 0ull;
}

void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool set_active) {
  if (!in_bounds(x) || !in_bounds(y) || !in_bounds(z))
    return;

  uint32_t idx = voxel_linear_index_u32(x, y, z);
  uint32_t w = idx >> 6;
  uint32_t b = idx & 63u;
  uint64_t m = (1ull << b);

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

  const uint32_t level_count = (uint32_t)TREE_LEVELS;

  // Dense pyramid:
  // level 0 = leaf brick masks (4x4x4 voxels => 64-bit mask)
  // level d>0 = parent masks (4x4x4 of previous level)
  uint64_t *level_masks[TREE_LEVELS];
  NodeState *level_state[TREE_LEVELS];
  uint32_t level_node_count[TREE_LEVELS];

  // Counts still based on WORDS_PER_CHUNK, /64, /64...
  // NOTE: Here WORDS_PER_CHUNK must equal (CHUNK_SIZE/4)^3.
  uint64_t current = (uint64_t)WORDS_PER_CHUNK;
  for (uint32_t d = 0; d < level_count; ++d) {
    level_node_count[d] = (uint32_t)current;
    level_masks[d] = (uint64_t *)calloc((size_t)current, sizeof(uint64_t));
    level_state[d] = (NodeState *)calloc((size_t)current, sizeof(NodeState));

    current = (current + 63ull) / 64ull;
    if (current == 0)
      current = 1;
  }

  // Axis per level (leaf bricks are CHUNK_SIZE/4 per axis, then /4 each level)
  uint32_t axis[TREE_LEVELS];
  axis[0] = (uint32_t)CHUNK_SIZE / 4u; // e.g. 64/4=16
  for (uint32_t d = 1; d < level_count; ++d) {
    axis[d] = axis[d - 1] / 4u; // 16 -> 4 -> 1 (TREE_LEVELS=3)
  }

  // ----------------------------
  // (A) Build leaf brick masks from dense voxel bitset (LINEAR)
  // ----------------------------
  {
    const uint32_t bricksAxis = axis[0];

    for (uint32_t bz = 0; bz < bricksAxis; ++bz) {
      for (uint32_t by = 0; by < bricksAxis; ++by) {
        for (uint32_t bx = 0; bx < bricksAxis; ++bx) {
          uint64_t mask = 0ull;

          for (uint32_t lz = 0; lz < 4u; ++lz) {
            for (uint32_t ly = 0; ly < 4u; ++ly) {
              for (uint32_t lx = 0; lx < 4u; ++lx) {
                uint32_t x = bx * 4u + lx;
                uint32_t y = by * 4u + ly;
                uint32_t z = bz * 4u + lz;

                uint32_t vidx = voxel_linear_index_u32((int)x, (int)y, (int)z);
                uint32_t w = vidx >> 6;
                uint32_t b = vidx & 63u;
                uint64_t on = (chunk->bits[w] >> b) & 1ull;

                uint32_t slot = slot_linear_4x4x4(lx, ly, lz);
                mask |= (on << slot);
              }
            }
          }

          uint32_t leafIndex = idx3_linear_u32(bx, by, bz, bricksAxis);
          level_masks[0][leafIndex] = mask;

          if (mask == 0ull)
            level_state[0][leafIndex] = NODE_EMPTY;
          else if (mask == ~0ull)
            level_state[0][leafIndex] = NODE_FULL;
          else
            level_state[0][leafIndex] = NODE_MIXED;
        }
      }
    }
  }

  // ----------------------------
  // (B) Build parents bottom-up (LINEAR 4x4x4 grouping)
  // ----------------------------
  for (uint32_t d = 1; d < level_count; ++d) {
    uint32_t C = axis[d - 1]; // child axis
    uint32_t P = axis[d];     // parent axis

    for (uint32_t pz = 0; pz < P; ++pz) {
      for (uint32_t py = 0; py < P; ++py) {
        for (uint32_t px = 0; px < P; ++px) {

          bool all_empty = true;
          bool all_full = true;
          uint64_t mask = 0ull;

          for (uint32_t cz = 0; cz < 4u; ++cz) {
            for (uint32_t cy = 0; cy < 4u; ++cy) {
              for (uint32_t cx = 0; cx < 4u; ++cx) {

                uint32_t child_x = px * 4u + cx;
                uint32_t child_y = py * 4u + cy;
                uint32_t child_z = pz * 4u + cz;

                uint32_t childDense = idx3_linear_u32(child_x, child_y, child_z, C);
                NodeState cs = level_state[d - 1][childDense];

                if (cs != NODE_EMPTY)
                  all_empty = false;
                if (cs != NODE_FULL)
                  all_full = false;

                uint32_t slot = slot_linear_4x4x4(cx, cy, cz);
                if (cs != NODE_EMPTY)
                  mask |= (1ull << slot);
              }
            }
          }

          uint32_t parentDense = idx3_linear_u32(px, py, pz, P);

          NodeState ps;
          if (all_empty)
            ps = NODE_EMPTY;
          else if (all_full)
            ps = NODE_FULL;
          else
            ps = NODE_MIXED;

          level_state[d][parentDense] = ps;

          if (ps == NODE_EMPTY)
            level_masks[d][parentDense] = 0ull;
          else if (ps == NODE_FULL)
            level_masks[d][parentDense] = ~0ull;
          else
            level_masks[d][parentDense] = mask;
        }
      }
    }
  }

  // ----------------------------
  // (C) Flatten sparsely in BFS order
  // ----------------------------
  Vector curQ, nextQ;
  vec_init(&curQ, sizeof(WorkItem), NULL);
  vec_init(&nextQ, sizeof(WorkItem), NULL);

  // Emit root always (dense index 0 at top)
  {
    uint32_t top = level_count - 1;
    Node rootN = {0};
    ChildIndex rootC = {0};

    rootN.mask = level_masks[top][0];
    rootC.first_child_index = 0u;

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

      Node *outN = VEC_AT(&chunk->nodes, w->out, Node);
      outN->mask = mask;

      ChildIndex *outCI = VEC_AT(&chunk->child_indices, w->out, ChildIndex);

      // Leaf decision:
      // - true leaf at d==0 (brick leaf: mask bits are voxels)
      // - early leaf if FULL at d>0 (mask = ~0 => solid at that resolution)
      bool is_leaf = (d == 0) || (state == NODE_FULL);

      if (is_leaf) {
        outCI->first_child_index = LEAF_BIT; // base ignored
        continue;
      }

      if (state == NODE_EMPTY || mask == 0ull) {
        outCI->first_child_index = 0u;
        continue;
      }

      // Mixed internal node
      uint32_t base = (uint32_t)vec_len(&chunk->nodes);
      outCI->first_child_index = (base & INDEX_MASK); // leaf bit clear

      // parent coords at this level
      uint32_t Np = axis[d];
      uint32_t px, py, pz;
      idx_to_xyz_u32(w->dense, Np, &px, &py, &pz);

      uint32_t Nc = axis[d - 1]; // since d>0 here

      for (uint32_t slot = 0; slot < 64u; ++slot) {
        if (((mask >> slot) & 1ull) == 0ull)
          continue;

        uint32_t cx = (slot) & 3u;
        uint32_t cy = (slot >> 2u) & 3u;
        uint32_t cz = (slot >> 4u) & 3u;

        uint32_t child_x = px * 4u + cx;
        uint32_t child_y = py * 4u + cy;
        uint32_t child_z = pz * 4u + cz;

        uint32_t childDense = idx3_linear_u32(child_x, child_y, child_z, Nc);

        // Safety
        uint32_t childCount = level_node_count[d - 1];
        if (childDense >= childCount)
          continue;

        NodeState cs = level_state[d - 1][childDense];
        if (cs == NODE_EMPTY)
          continue;

        uint64_t childMask = level_masks[d - 1][childDense];

        uint32_t childOut = (uint32_t)vec_len(&chunk->nodes);

        Node cn = {0};
        ChildIndex cci = {0};
        cn.mask = childMask;
        cci.first_child_index = 0u;

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

  // Debug prints (optional)
  LOG_INFO("Print indices");
  for (u32 i = 0; i < chunk->child_indices.length; i++) {
    LOG_INFO("index: %d, Value: %u", (int)i, VEC_AT(&chunk->child_indices, i, ChildIndex)->first_child_index);
  }

  LOG_INFO("Print Nodes");
  for (u32 i = 0; i < chunk->nodes.length; i++) {
    LOG_INFO("index: %d, Value: %lu", (int)i, (unsigned long)VEC_AT(&chunk->nodes, i, Node)->mask);
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

// --- Private Functions ---
static void _set_box(ChunkTree *chunk, vec3 pos, u32 size) {
  int px = (int)pos[0];
  int py = (int)pos[1];
  int pz = (int)pos[2];

  for (u32 x = 0; x < size; x++) {
    for (u32 y = 0; y < size; y++) {
      for (u32 z = 0; z < size; z++) {
        chunk_set_voxel(chunk, px + (int)x, py + (int)y, pz + (int)z, true);
      }
    }
  }
}

static inline bool in_bounds(int v) { return (v >= 0) && (v < (int)CHUNK_SIZE); }
