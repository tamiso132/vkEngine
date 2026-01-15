//! chunk_internal.h
#include "chunk_internal.h"

typedef struct WorkItem {
  u32 dense; // dense index within current level grid (linear 3D)
  u32 out;   // index in output arrays
} WorkItem;

typedef struct Pyramid {
  u32 level_count;
  u32 axis[TREE_LEVELS];
  u32 node_count[TREE_LEVELS];

  u64 *masks[TREE_LEVELS];
  NodeState *states[TREE_LEVELS];
} Pyramid;

// --- Private Prototypes ---
static inline u32 u32_pow3(u32 a);
static bool pyramid_init(Pyramid *p, u32 chunk_size, u32 tree_levels);
static void pyramid_free(Pyramid *p);
static u16 leaf_material_from_node(const ChunkBuildInput *in, u32 dense_idx, u32 axis_size, bool is_level0_brick);
static void build_leaf_level0(const ChunkBuildInput *in, u64 *leaf_masks, NodeState *leaf_states, u32 axis);
static void build_parent_level(u32 level, u64 **masks, NodeState **states, const u32 *axis);
static void flatten_tree_bfs(const ChunkBuildInput *in, const Pyramid *p, ChunkBuildOutput *out);
static bool in_bounds(int v);
static u32 voxel_linear_index_u32(int x, int y, int z);
static u32 idx3_linear_u32(u32 x, u32 y, u32 z, u32 N);
static void idx_to_xyz_u32(u32 idx, u32 N, u32 *x, u32 *y, u32 *z);
static u32 slot_linear_4x4x4(u32 lx, u32 ly, u32 lz);
static u32 xorshift32(u32 *state);
static float rand01(u32 *state);

// ----------------------------
// Public API
// ----------------------------

ChunkBuildResult _build_chunk(const ChunkBuildInput *in, ChunkBuildScratch *scratch, ChunkBuildOutput *out) {

  if (!in || !out || !in->bits || !in->vox_mat)
    return CHUNK_BUILD_ERR_BAD_CONFIG;
  if (in->chunk_size == 0 || (in->chunk_size % 4u) != 0u)
    return CHUNK_BUILD_ERR_BAD_CONFIG;
  if (in->tree_levels == 0 || in->tree_levels > TREE_LEVELS)
    return CHUNK_BUILD_ERR_BAD_CONFIG;

  vec_clear(out->nodes);
  vec_clear(out->child_indices);
  vec_clear(out->leaf_mats);

  Pyramid p;
  if (!pyramid_init(&p, in->chunk_size, in->tree_levels)) {
    pyramid_free(&p);
    _build_chunk_free(out);
    return CHUNK_BUILD_ERR_OOM;
  }

  build_leaf_level0(in, p.masks[0], p.states[0], p.axis[0]);
  for (u32 d = 1; d < p.level_count; ++d)
    build_parent_level(d, p.masks, p.states, p.axis);

  flatten_tree_bfs(in, &p, out);
  pyramid_free(&p);

  for (u32 i = 0; i < out->child_indices->length; i++) {
    LOG_INFO("Child Index(%d) = %u", i, VEC_AT(out->child_indices, i, ChildIndex)->first_child_index);
  }

  for (u32 i = 0; i < out->nodes->length; i++) {
    LOG_INFO("Node Index(%d) = %lu", i, VEC_AT(out->nodes, i, Node)->mask);
  }

  return CHUNK_BUILD_OK;
}

void _build_chunk_free(ChunkBuildOutput *out) {
  if (!out)
    return;
  if (out->nodes->data)
    free(out->nodes->data);
  if (out->child_indices->data)
    free(out->child_indices->data);
  memset(out, 0, sizeof(*out));
}

// --- Private Functions ---

// ----------------------------
// Local helpers
// ----------------------------
static inline u32 u32_pow3(u32 a) { return a * a * a; }

static bool pyramid_init(Pyramid *p, u32 chunk_size, u32 tree_levels) {
  memset(p, 0, sizeof(*p));
  p->level_count = tree_levels;

  // Level 0 is bricks: (chunk_size/4)^3
  p->axis[0] = chunk_size / 4u;
  if (p->axis[0] == 0u)
    return false;

  for (u32 d = 0; d < p->level_count; ++d) {
    if (d > 0)
      p->axis[d] = p->axis[d - 1] / 4u;
    if (p->axis[d] == 0u)
      return false;

    p->node_count[d] = u32_pow3(p->axis[d]);

    p->masks[d] = (u64 *)calloc((size_t)p->node_count[d], sizeof(u64));
    p->states[d] = (NodeState *)calloc((size_t)p->node_count[d], sizeof(NodeState));
    if (!p->masks[d] || !p->states[d])
      return false;
  }
  return true;
}

static void pyramid_free(Pyramid *p) {
  for (u32 d = 0; d < p->level_count; ++d) {
    free(p->masks[d]);
    free(p->states[d]);
    p->masks[d] = NULL;
    p->states[d] = NULL;
  }
}

static u16 leaf_material_from_node(const ChunkBuildInput *in, u32 dense_idx, u32 axis_size, bool is_level0_brick) {
  const u32 chunk_size = in->chunk_size;

  if (!is_level0_brick) {
    // For FULL internal nodes, sample one voxel within the covered region.
    u32 px, py, pz;
    idx_to_xyz_u32(dense_idx, axis_size, &px, &py, &pz);

    u32 scale = chunk_size / axis_size;
    u32 vx = px * scale;
    u32 vy = py * scale;
    u32 vz = pz * scale;

    return in->vox_mat[voxel_linear_index_u32((int)vx, (int)vy, (int)vz)];
  }

  // Level-0 bricks: find first set voxel and return its material.
  u32 bx, by, bz;
  idx_to_xyz_u32(dense_idx, axis_size, &bx, &by, &bz);

  for (u32 i = 0; i < 64u; ++i) {
    u32 lx = i & 3u;
    u32 ly = (i >> 2u) & 3u;
    u32 lz = (i >> 4u) & 3u;

    u32 vx = bx * 4u + lx;
    u32 vy = by * 4u + ly;
    u32 vz = bz * 4u + lz;

    u32 vidx = voxel_linear_index_u32((int)vx, (int)vy, (int)vz);
    if ((in->bits[vidx >> 6] >> (vidx & 63u)) & 1ull)
      return in->vox_mat[vidx];
  }

  return 0;
}

// Stage A: leaf masks
static void build_leaf_level0(const ChunkBuildInput *in, u64 *leaf_masks, NodeState *leaf_states, u32 axis) {
  for (u32 bz = 0; bz < axis; ++bz) {
    for (u32 by = 0; by < axis; ++by) {
      for (u32 bx = 0; bx < axis; ++bx) {
        u64 mask = 0ull;

        for (u32 lz = 0; lz < 4u; ++lz)
          for (u32 ly = 0; ly < 4u; ++ly)
            for (u32 lx = 0; lx < 4u; ++lx) {
              u32 x = bx * 4u + lx;
              u32 y = by * 4u + ly;
              u32 z = bz * 4u + lz;

              u32 vidx = voxel_linear_index_u32((int)x, (int)y, (int)z);
              if ((in->bits[vidx >> 6] >> (vidx & 63u)) & 1ull)
                mask |= (1ull << slot_linear_4x4x4(lx, ly, lz));
            }

        u32 idx = idx3_linear_u32(bx, by, bz, axis);
        leaf_masks[idx] = mask;

        if (mask == 0ull)
          leaf_states[idx] = NODE_EMPTY;
        else if (mask == ~0ull)
          leaf_states[idx] = NODE_FULL;
        else
          leaf_states[idx] = NODE_MIXED;
      }
    }
  }
}

// Stage B: parent masks
static void build_parent_level(u32 level, u64 **masks, NodeState **states, const u32 *axis) {
  const u32 child_axis = axis[level - 1];
  const u32 parent_axis = axis[level];

  for (u32 pz = 0; pz < parent_axis; ++pz) {
    for (u32 py = 0; py < parent_axis; ++py) {
      for (u32 px = 0; px < parent_axis; ++px) {
        bool all_empty = true;
        bool all_full = true;
        u64 occ_mask = 0ull;

        for (u32 i = 0; i < 64u; ++i) {
          u32 cx = (i) & 3u;
          u32 cy = (i >> 2u) & 3u;
          u32 cz = (i >> 4u) & 3u;

          u32 c_idx = idx3_linear_u32(px * 4u + cx, py * 4u + cy, pz * 4u + cz, child_axis);
          NodeState cs = states[level - 1][c_idx];

          if (cs != NODE_EMPTY) {
            all_empty = false;
            occ_mask |= (1ull << i);
          }
          if (cs != NODE_FULL)
            all_full = false;
        }

        u32 p_idx = idx3_linear_u32(px, py, pz, parent_axis);
        NodeState ps;
        if (all_empty)
          ps = NODE_EMPTY;
        else if (all_full)
          ps = NODE_FULL;
        else
          ps = NODE_MIXED;

        states[level][p_idx] = ps;
        masks[level][p_idx] = (ps == NODE_FULL) ? ~0ull : occ_mask;
      }
    }
  }
}

// Stage C: flatten
static void flatten_tree_bfs(const ChunkBuildInput *in, const Pyramid *p, ChunkBuildOutput *out) {
  Vector curQ, nextQ;
  vec_init(&curQ, sizeof(WorkItem), NULL);
  vec_init(&nextQ, sizeof(WorkItem), NULL);

  const u32 root_level = p->level_count - 1u;
  Node rootN = {.mask = p->masks[root_level][0]};
  ChildIndex rootC = {.first_child_index = 0u};

  vec_push(out->nodes, &rootN);
  vec_push(out->child_indices, &rootC);

  WorkItem rootW = {.dense = 0u, .out = 0u};
  vec_push(&curQ, &rootW);

  for (int d = (int)root_level; d >= 0; --d) {
    vec_clear(&nextQ);
    const u32 axis_d = p->axis[d];

    for (u32 i = 0; i < vec_len(&curQ); ++i) {
      const WorkItem *w = VEC_AT(&curQ, i, WorkItem);

      u64 mask = p->masks[d][w->dense];
      NodeState st = p->states[d][w->dense];

      Node *outN = VEC_AT(out->nodes, w->out, Node);
      ChildIndex *outCI = VEC_AT(out->child_indices, w->out, ChildIndex);
      outN->mask = mask;

      // Leaf: d==0 OR FULL
      if (d == 0 || st == NODE_FULL) {
        // Calculate where this block of 64 materials will start
        // Assuming leaf_mats holds u16.
        // We pack 64 materials per leaf.
        u32 mat_offset = (u32)(vec_len(out->leaf_mats) / 64);

        // We must push 64 materials (one for each bit in the mask)
        u32 bx, by, bz;
        idx_to_xyz_u32(w->dense, axis_d, &bx, &by, &bz);

        for (u32 slot = 0; slot < 64; ++slot) {
          u16 mat = 0;

          // Only fetch material if the voxel exists in the mask
          if ((mask >> slot) & 1ull) {
            if (d == 0) {
              // --- Level 0: Actual Voxel Lookup ---
              u32 lx = slot & 3u;
              u32 ly = (slot >> 2u) & 3u;
              u32 lz = (slot >> 4u) & 3u;

              u32 vx = bx * 4u + lx;
              u32 vy = by * 4u + ly;
              u32 vz = bz * 4u + lz;

              // Direct lookup into input voxel grid
              u32 vidx = voxel_linear_index_u32((int)vx, (int)vy, (int)vz);
              mat = in->vox_mat[vidx];
            } else {
              // --- Level > 0: "Full" Node Optimization ---
              // This node covers a large area but is solid.
              // We fill all 64 slots with a representative material
              // (e.g., sample the corner or center).
              // Reusing your existing logic for single sampling:
              mat = leaf_material_from_node(in, w->dense, axis_d, false);
            }
          }

          vec_push(out->leaf_mats, &mat);
        }

        // Store index: LEAF_BIT | (offset into leaf_mats buffer)
        outCI->first_child_index = LEAF_BIT | (mat_offset & INDEX_MASK);
        continue;
      }

      // Internal
      u32 base_idx = (u32)vec_len(out->nodes);
      outCI->first_child_index = (base_idx & INDEX_MASK);

      u32 px, py, pz;
      idx_to_xyz_u32(w->dense, axis_d, &px, &py, &pz);
      const u32 child_axis = p->axis[d - 1];

      for (u32 slot = 0; slot < 64u; ++slot) {
        if (!((mask >> slot) & 1ull))
          continue;

        u32 cx = slot & 3u;
        u32 cy = (slot >> 2u) & 3u;
        u32 cz = (slot >> 4u) & 3u;

        u32 c_idx = idx3_linear_u32(px * 4u + cx, py * 4u + cy, pz * 4u + cz, child_axis);
        if (p->states[d - 1][c_idx] == NODE_EMPTY)
          continue;

        Node cn = {.mask = p->masks[d - 1][c_idx]};
        ChildIndex cci = {.first_child_index = 0u};

        vec_push(out->nodes, &cn);
        vec_push(out->child_indices, &cci);

        WorkItem childW = {.dense = c_idx, .out = (u32)(vec_len(out->nodes) - 1u)};
        vec_push(&nextQ, &childW);
      }
    }

    Vector tmp = curQ;
    curQ = nextQ;
    nextQ = tmp;
    if (d == 0)
      break;
  }

  vec_free(&curQ);
  vec_free(&nextQ);
}

static bool in_bounds(int v) { return (v >= 0) && (v < (int)CHUNK_SIZE); }

// -------------------- Linear helpers --------------------
// idx = x + N*(y + N*z)

// 24-bit fraction -> [0,1)
static float rand01(u32 *state) { return (xorshift32(state) & 0x00FFFFFFu) / 16777216.0f; }
