#include "chunk_internal.h"
#include <stdlib.h>
#include <string.h>

typedef struct WorkItem {
  uint32_t dense; // dense index within level d grid (linear 3D)
  uint32_t out;   // compact node index in chunk->nodes/child_indices
} WorkItem;

// --- Private Prototypes ---
static u16 _extract_leaf_material(ChunkTree *chunk, u32 dense_idx, u32 axis_size, bool is_brick);
static void _generate_leaf_masks(ChunkTree *chunk, uint64_t *leaf_masks, NodeState *leaf_states, uint32_t axis);
static void _generate_parent_masks(uint32_t level, uint64_t **masks, NodeState **states, uint32_t *axes);
static void _flatten_tree_bfs(ChunkTree *chunk, uint64_t **masks, NodeState **states, uint32_t *axes, uint32_t level_count);




// --- Public Implementation ---

void chunk_rebuild(ChunkTree *chunk) {
    if (!chunk->is_dirty) return;

    vec_clear(&chunk->nodes);
    vec_clear(&chunk->child_indices);

    const uint32_t level_count = (uint32_t)TREE_LEVELS;
    
    // Allocate temporary dense pyramids
    uint64_t *level_masks[TREE_LEVELS];
    NodeState *level_state[TREE_LEVELS];
    uint32_t axis[TREE_LEVELS];
    uint32_t level_node_count[TREE_LEVELS];

    // Initialize dimensions and allocations
    uint64_t current_words = (uint64_t)WORDS_PER_CHUNK;
    axis[0] = (uint32_t)CHUNK_SIZE / 4u;

    for (uint32_t d = 0; d < level_count; ++d) {
        if (d > 0) axis[d] = axis[d - 1] / 4u;
        
        level_node_count[d] = (uint32_t)current_words;
        level_masks[d] = (uint64_t *)calloc((size_t)current_words, sizeof(uint64_t));
        level_state[d] = (NodeState *)calloc((size_t)current_words, sizeof(NodeState));

        current_words = (current_words + 63ull) / 64ull;
        if (current_words == 0) current_words = 1;
    }

    // Stage A: Leaf Generation
    _generate_leaf_masks(chunk, level_masks[0], level_state[0], axis[0]);

    // Stage B: Bottom-up Mipmapping
    for (uint32_t d = 1; d < level_count; ++d) {
        _generate_parent_masks(d, level_masks, level_state, axis);
    }

    // Stage C: Flattening (BFS)
    _flatten_tree_bfs(chunk, level_masks, level_state, axis, level_count);

    // Cleanup
    for (uint32_t d = 0; d < level_count; ++d) {
        free(level_masks[d]);
        free(level_state[d]);
    }

    chunk->is_dirty = false;
    chunk->need_upload = true;
}

// --- Helper Implementations ---

static void _generate_leaf_masks(ChunkTree *chunk, uint64_t *leaf_masks, NodeState *leaf_states, uint32_t axis) {
    for (uint32_t bz = 0; bz < axis; ++bz) {
        for (uint32_t by = 0; by < axis; ++by) {
            for (uint32_t bx = 0; bx < axis; ++bx) {
                uint64_t mask = 0ull;

                // Process 4x4x4 block
                for (uint32_t lz = 0; lz < 4u; ++lz) {
                    for (uint32_t ly = 0; ly < 4u; ++ly) {
                        for (uint32_t lx = 0; lx < 4u; ++lx) {
                            uint32_t x = bx * 4u + lx;
                            uint32_t y = by * 4u + ly;
                            uint32_t z = bz * 4u + lz;

                            uint32_t vidx = voxel_linear_index_u32(x, y, z);
                            if ((chunk->bits[vidx >> 6] >> (vidx & 63u)) & 1ull) {
                                mask |= (1ull << slot_linear_4x4x4(lx, ly, lz));
                            }
                        }
                    }
                }

                uint32_t idx = idx3_linear_u32(bx, by, bz, axis);
                leaf_masks[idx] = mask;
                
                if (mask == 0ull) leaf_states[idx] = NODE_EMPTY;
                else if (mask == ~0ull) leaf_states[idx] = NODE_FULL;
                else leaf_states[idx] = NODE_MIXED;
            }
        }
    }
}

static void _generate_parent_masks(uint32_t level, uint64_t **masks, NodeState **states, uint32_t *axes) {
    uint32_t c_axis = axes[level - 1];
    uint32_t p_axis = axes[level];

    for (uint32_t pz = 0; pz < p_axis; ++pz) {
        for (uint32_t py = 0; py < p_axis; ++py) {
            for (uint32_t px = 0; px < p_axis; ++px) {
                
                bool all_empty = true;
                bool all_full = true;
                uint64_t mask = 0ull;

                for (uint32_t i = 0; i < 64; ++i) {
                    uint32_t cx = (i) & 3u;
                    uint32_t cy = (i >> 2u) & 3u;
                    uint32_t cz = (i >> 4u) & 3u;

                    uint32_t c_idx = idx3_linear_u32(px*4+cx, py*4+cy, pz*4+cz, c_axis);
                    NodeState cs = states[level - 1][c_idx];

                    if (cs != NODE_EMPTY) {
                        all_empty = false;
                        mask |= (1ull << i);
                    }
                    if (cs != NODE_FULL) all_full = false;
                }

                uint32_t p_idx = idx3_linear_u32(px, py, pz, p_axis);
                
                if (all_empty) states[level][p_idx] = NODE_EMPTY;
                else if (all_full) states[level][p_idx] = NODE_FULL;
                else states[level][p_idx] = NODE_MIXED;

                masks[level][p_idx] = (states[level][p_idx] == NODE_FULL) ? ~0ull : mask;
            }
        }
    }
}

static void _flatten_tree_bfs(ChunkTree *chunk, uint64_t **masks, NodeState **states, uint32_t *axes, uint32_t level_count) {
    Vector curQ, nextQ;
    vec_init(&curQ, sizeof(WorkItem), NULL);
    vec_init(&nextQ, sizeof(WorkItem), NULL);

    // Push Root
    Node rootN = { .mask = masks[level_count - 1][0] };
    ChildIndex rootC = { .first_child_index = 0u };
    vec_push(&chunk->nodes, &rootN);
    vec_push(&chunk->child_indices, &rootC);
    
    WorkItem rootW = {0, 0};
    vec_push(&curQ, &rootW);

    for (int d = (int)level_count - 1; d >= 0; --d) {
        vec_clear(&nextQ);
        
        for (u32 i = 0; i < vec_len(&curQ); ++i) {
            WorkItem *w = VEC_AT(&curQ, i, WorkItem);
            uint64_t mask = masks[d][w->dense];
            NodeState state = states[d][w->dense];

            // Update node in final array
            Node *outN = VEC_AT(&chunk->nodes, w->out, Node);
            outN->mask = mask;
            ChildIndex *outCI = VEC_AT(&chunk->child_indices, w->out, ChildIndex);

            // 1. Leaf Logic
            if (d == 0 || state == NODE_FULL) {
                u16 mat = 0;
                if (mask != 0) {
                     mat = _extract_leaf_material(chunk, w->dense, axes[d], d == 0);
                }
                outCI->first_child_index = LEAF_BIT | ((u32)mat & INDEX_MASK);
                continue;
            }

            // 2. Internal Node Logic
            uint32_t base_idx = (uint32_t)vec_len(&chunk->nodes);
            outCI->first_child_index = (base_idx & INDEX_MASK);

            uint32_t px, py, pz;
            idx_to_xyz_u32(w->dense, axes[d], &px, &py, &pz);
            uint32_t child_axis = axes[d-1];

            // 3. Spawn Children
            for (uint32_t slot = 0; slot < 64u; ++slot) {
                if (!((mask >> slot) & 1ull)) continue;

                uint32_t cx = slot & 3u, cy = (slot>>2)&3u, cz = (slot>>4)&3u;
                uint32_t c_idx = idx3_linear_u32(px*4+cx, py*4+cy, pz*4+cz, child_axis);
                
                if (states[d-1][c_idx] == NODE_EMPTY) continue;

                Node cn = { .mask = masks[d-1][c_idx] };
                ChildIndex cci = { 0 };
                
                vec_push(&chunk->nodes, &cn);
                vec_push(&chunk->child_indices, &cci);
                
                WorkItem childW = { .dense = c_idx, .out = (u32)(vec_len(&chunk->nodes)-1) };
                vec_push(&nextQ, &childW);
            }
        }
        
        // Swap queues
        Vector tmp = curQ; curQ = nextQ; nextQ = tmp;
        if (d == 0) break;
    }

    vec_free(&curQ);
    vec_free(&nextQ);
}

static u16 _extract_leaf_material(ChunkTree *chunk, u32 dense_idx, u32 axis_size, bool is_brick) {
    if (!is_brick) {
        // For FULL internal nodes, just sample the corner
        u32 px, py, pz;
        idx_to_xyz_u32(dense_idx, axis_size, &px, &py, &pz);
        // Scale coordinate up to voxel space
        u32 scale = CHUNK_SIZE / axis_size; 
        return chunk->vox_mat[voxel_linear_index_u32(px*scale, py*scale, pz*scale)];
    }

    // For Brick leaves, search for first active voxel to get material
    u32 bx, by, bz;
    idx_to_xyz_u32(dense_idx, axis_size, &bx, &by, &bz);
    for(u32 i=0; i<64; ++i) {
        u32 lx = i&3, ly = (i>>2)&3, lz = (i>>4)&3;
        u32 vidx = voxel_linear_index_u32(bx*4+lx, by*4+ly, bz*4+lz);
        if((chunk->bits[vidx>>6] >> (vidx&63)) & 1) {
            return chunk->vox_mat[vidx];
        }
    }
    return 0;
}