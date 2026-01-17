
#ifndef RAYTRACE_TRAVERSAL_GLSL
#define RAYTRACE_TRAVERSAL_GLSL

#include "rt_bindings.glsl"
#include "rt_shared.glsl"
#include "rt_occulsion.glsl"

// -----------------------------------------
// Local helpers
// -----------------------------------------
uint leaf_index_for_slot(uint packed, uint64_t node_mask, uint slot)
{
        // packed contains base leaf index in low 31 bits (your INDEX_MASK)
        uint base_idx = packed & INDEX_MASK;

        // count how many occupied bits before 'slot' to compute offset
        uint64_t lower_mask = (u64(1) << slot) - u64(1);
        uint offset = popcount64(node_mask & lower_mask);

        return base_idx + offset;
}

#define GET_LEAF_MATERIAL(buf_id, idx) \
  ((g_leaf_mats[nonuniformEXT(buf_id)].data[(idx) >> 1u] >> (((idx) & 1u) << 4u)) & 0xFFFFu)

// -----------------------------------------
// Public API
// -----------------------------------------
bool traverse_one_chunk(
        Ray ray,
        vec3 chunk_min,
        out vec3 hit_pos,
        out uint hit_level,
        out uint hit_slot,
        out uint iter_count,
        out uint err_code,
        out vec3 hit_color,
        out vec3 n_smooth,
        out float ao)
{
        hit_pos = vec3(0.0);
        hit_level = 0u;
        hit_slot = 0u;
        iter_count = 0u;
        err_code = TRACE_OK;
        hit_color = vec3(0.0);

        const int MAX_ITER = 4096;
        const int STACK_CAP = TREE_LEVELS + 1;
        const uint NODE_STEP_CAP = 256u; // 4x4x4 = 64, cap higher for eps/boundary cases
        const int STUCK_LIMIT = 64;

        uint levels = uint(TREE_LEVELS);

        float chunk_size = float(CHUNK_SIZE);
        vec3 chunk_max = chunk_min + vec3(chunk_size);

        float t0, t1;
        if (!ray_aabb(ray.origin, ray.dir, chunk_min, chunk_max, t0, t1)) {
                err_code = TRACE_OK; // miss, not an error
                return false;
        }

        #ifdef SHADER_DEBUG
        if (all(lessThan(abs(ray.dir), vec3(1e-12)))) {
                err_code = TRACE_ERR_DIR_ZERO;
                return false;
        }
        if (any(isnan(ray.dir)) || any(isinf(ray.dir)) ||
                        any(isnan(ray.origin)) || any(isinf(ray.origin))) {
                err_code = TRACE_ERR_DIR_NAN_INF;
                return false;
        }
        #endif

        TraverseFrame stack[STACK_CAP];
        int stack_top = 0;

        float cur_t_base = max(t0, 0.0);

        TraverseFrame cur;
        cur.node_index = 0u;
        cur.level = 0u;
        cur.node_min = chunk_min;
        cur.node_size = chunk_size;
        cur.t_base = cur_t_base;
        cur.steps_in_node = 0u;

        if (!init_node_dda(ray, cur.node_min, cur.node_size, cur_t_base, cur.dda)) {
                err_code = TRACE_ERR_INIT_NODE_FAIL;
                return false;
        }

        RayTraceConstState cs;
        cs.step_dir_i32 = ivec3(sign(ray.dir));

        int stuck_count = 0;

        for (int iter = 0; iter < MAX_ITER; ++iter)
        {
                if (cur.t_base > 1000) {
                        return false;
                }
                iter_count = uint(iter);

                #ifdef SHADER_DEBUG
                if (any(isnan(cur.dda.side_dist)) || any(isinf(cur.dda.side_dist)) ||
                                any(isnan(cur.dda.delta_dist)) || any(isinf(cur.dda.delta_dist)) ||
                                isnan(cur.dda.t_tot) || isinf(cur.dda.t_tot)) {
                        err_code = TRACE_ERR_DDA_NAN_INF;
                        return false;
                }
                #endif

                // -----------------------------------------
                // Ascend while out of bounds (leaving node)
                // -----------------------------------------
                while (cur.dda.out_of_bounds)
                {
                        // Popped above root => exited chunk: miss
                        if (stack_top == 0) {
                                err_code = TRACE_OK;
                                return false;
                        }

                        // restore parent
                        cur = stack[--stack_top];
                        cur_t_base = cur.t_base;

                        // step parent once to move off exhausted child
                        uint64_t parent_mask = G_NODE_MASK(pc, cur.node_index);
                        cs.occupancy_mask = parent_mask;

                        ivec3 prev_pos = cur.dda.local_pos;
                        vec3 prev_side = cur.dda.side_dist;
                        float prev_t = cur.dda.t_tot;

                        dda_step(cur.dda, cs);
                        cur.steps_in_node++;
                        #if OPTIMIZATION_RT_JUMP_EMPTY_SPACE
                        uint64_t pmask = G_NODE_MASK(pc, cur.node_index);
                        uint64_t reach = reachable_mask_4x4x4(cur.dda.local_pos, cs.step_dir_i32);
                        if ((pmask & reach) == u64(0)) {
                                dda_skip_to_exit(cur.dda, cs);
                        }
                        #endif

                        #ifdef SHADER_DEBUG
                        if (cur.steps_in_node > NODE_STEP_CAP) {
                                err_code = TRACE_ERR_NODE_STEP_CAP;
                                return false;
                        }
                        bool no_change =
                                all(equal(cur.dda.local_pos, prev_pos)) &&
                                        all(equal(cur.dda.side_dist, prev_side)) &&
                                        (cur.dda.t_tot == prev_t);

                        if (no_change) {
                                stuck_count++;
                                if (stuck_count > STUCK_LIMIT) {
                                        err_code = TRACE_ERR_DDA_STUCK;
                                        return false;
                                }
                        } else {
                                stuck_count = 0;
                        }
                        #endif
                }

                #ifdef SHADER_DEBUG
                if (cur.level >= levels) {
                        err_code = TRACE_ERR_LEVEL_OOB;
                        return false;
                }
                #endif

                // fetch node data
                uint64_t mask = G_NODE_MASK(pc, cur.node_index);
                uint packed = G_CHILD_PACK(pc, cur.node_index);

                bool level_leaf = (cur.level == (levels - 1u));
                bool early_leaf = childindex_is_leaf(packed);

                #ifdef SHADER_DEBUG
                if (any(lessThan(cur.dda.local_pos, ivec3(0))) ||
                                any(greaterThan(cur.dda.local_pos, ivec3(3)))) {
                        // better: TRACE_ERR_LOCALPOS_OOB, but keep your existing code if needed
                        err_code = TRACE_ERR_DDA_NAN_INF;
                        return false;
                }
                #endif

                uint slot = get_local_index_from_vec3(cur.dda.local_pos);
                bool cell_occupied = child_present(mask, slot);

                // -----------------------------------------
                // Leaf handling
                // -----------------------------------------
                if (level_leaf || early_leaf)
                {
                        if (cell_occupied)
                        {
                                float t_hit = cur_t_base + cur.dda.t_tot;
                                hit_pos = ray.origin + t_hit * ray.dir;
                                hit_level = cur.level;
                                hit_slot = slot;
                                err_code = TRACE_OK;

                                uint leaf_idx = leaf_index_for_slot(packed, mask, slot);
                                uint mat_id = GET_LEAF_MATERIAL(pc.leaf_mats_id, leaf_idx);
                                hit_color = u32_rgb8_to_vec3(G_PAL(pc, mat_id));

                                vec3 hit_n = vec3(cur.dda.prev_step_i32);
                                if (dot(hit_n, hit_n) < 0.5) hit_n = normalize(-ray.dir); // fallback
                                else hit_n = normalize(hit_n);

                                vec3 p0 = (hit_pos - chunk_min) - hit_n * 1e-3; // chunk-local continuous

                                ao_and_normal_soft(p0, hit_n, ao, n_smooth);

                                hit_color *= ao;

                                #ifdef LIGHTING
                                float ndotl = max(0.0, dot(n_smooth, vec3(0, 1, 0)));
                                hit_color *= ndotl;
                                #endif

                                return true;
                        }
                }
                // -----------------------------------------
                // Internal node: descend if occupied
                // -----------------------------------------
                else if (cell_occupied)
                {
                        uint child_index;
                        if (node_get_child_index(mask, packed, slot, child_index))
                        {
                                #ifdef SHADER_DEBUG
                                if (stack_top >= STACK_CAP) {
                                        err_code = TRACE_ERR_STACK_OVERFLOW;
                                        return false;
                                }
                                if (child_index == cur.node_index) {
                                        err_code = TRACE_ERR_CHILD_SELF_LOOP;
                                        return false;
                                }
                                #endif
                                cur.t_base = cur_t_base;

                                // push parent frame
                                stack[stack_top++] = cur;

                                float child_size = cur.node_size * 0.25;
                                vec3 child_min = cur.node_min + vec3(cur.dda.local_pos) * child_size;
                                float child_t_base = cur_t_base + cur.dda.t_tot;

                                // descend
                                cur.node_index = child_index;
                                cur.level = cur.level + 1u;
                                cur.node_min = child_min;
                                cur.node_size = child_size;
                                cur.t_base = child_t_base;
                                cur.steps_in_node = 0u;

                                cur_t_base = child_t_base;

                                if (!init_node_dda(ray, cur.node_min, cur.node_size, cur_t_base, cur.dda)) {
                                        err_code = TRACE_ERR_INIT_NODE_FAIL;
                                        if (stack_top == 0) return false;
                                        cur = stack[--stack_top];
                                        cur_t_base = cur.t_base;
                                } else {

                                        #if OPTIMIZATION_RT_JUMP_EMPTY_SPACE

                                        uint64_t cmask = G_NODE_MASK(pc, cur.node_index);
                                        uint64_t reach = reachable_mask_4x4x4(cur.dda.local_pos, cs.step_dir_i32);
                                        if ((cmask & reach) == u64(0)) {
                                                dda_skip_to_exit(cur.dda, cs);
                                                continue; // next loop iteration will pop/ascend immediately
                                        }

                                        #endif
                                        continue;
                                }
                        }
                }

                // -----------------------------------------
                // Step inside current node
                // -----------------------------------------
                cs.occupancy_mask = mask;

                ivec3 prev_pos = cur.dda.local_pos;
                vec3 prev_side = cur.dda.side_dist;
                float prev_t = cur.dda.t_tot;

                dda_step(cur.dda, cs);
                cur.steps_in_node++;

                #ifdef SHADER_DEBUG
                if (cur.steps_in_node > NODE_STEP_CAP) {
                        err_code = TRACE_ERR_NODE_STEP_CAP;
                        return false;
                }

                bool no_change =
                        all(equal(cur.dda.local_pos, prev_pos)) &&
                                all(equal(cur.dda.side_dist, prev_side)) &&
                                (cur.dda.t_tot == prev_t);

                if (no_change) {
                        stuck_count++;
                        if (stuck_count > STUCK_LIMIT) {
                                err_code = TRACE_ERR_DDA_STUCK;
                                return false;
                        }
                } else {
                        stuck_count = 0;
                }
                #endif
        }

        err_code = TRACE_ERR_MAX_ITER;
        iter_count = uint(MAX_ITER);
        return false;
}

#endif
