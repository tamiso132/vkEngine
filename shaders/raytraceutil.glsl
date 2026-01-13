#extension GL_ARB_shading_language_include : require
#include "raytrace.glsl"

bool childindex_is_leaf(uint packed_first_child_index)
{
        return (packed_first_child_index & LEAF_BIT) != 0u;
}

uint childindex_base(uint packed_first_child_index)
{
        return (packed_first_child_index & INDEX_MASK);
}

// ---------------------------
// Spatial helper: 4x4x4 child slot
// ---------------------------
uint child_slot_from_xyz(uvec3 p, uint level, uint levels)
{
        // level = 0 at root (largest cells), increasing toward leaf
        uint shift = 2u * (levels - 1u - level); // root uses top bits
        uint cx = (p.x >> shift) & 3u;
        uint cy = (p.y >> shift) & 3u;
        uint cz = (p.z >> shift) & 3u;
        return cx | (cy << 2u) | (cz << 4u); // 0..63
}

// ---------------------------
// 64-bit bit helpers
// ---------------------------
uint popcount64(uint64_t v)
{
        uint lo = uint(v);
        uint hi = uint(v >> 32);
        return uint(bitCount(lo) + bitCount(hi));
}

bool child_present(uint64_t mask, uint slot)
{
        return ((mask >> (slot & 63u)) & 1ul) != 0ul;
}

uint child_compact_offset(uint64_t mask, uint slot)
{
        // count bits strictly before slot
        uint s = (slot & 63u);
        if (s == 0u) return 0u; // avoid (1<<0)-1 edge paranoia
        uint64_t beforeMask = mask & ((1ul << s) - 1ul);
        return popcount64(beforeMask);
}

// ---------------------------
// Pure "next index" computation
// ---------------------------
// Inputs:
//   parentMask: parent's 64-bit occupancy mask
//   parentPackedChildIndex: packed (leaf flag + base index)
//   slot: 0..63
//
// Output:
//   childNodeIndex = base + popcount(mask bits before slot)
// Returns false if child not present or parent is leaf.
bool node_get_child_index_pure(
        uint64_t parent_mask,
        uint parent_child_index,
        uint slot,
        out uint child_index)
{
        if (childindex_is_leaf(parent_child_index))
                return false;

        if (!child_present(parent_mask, slot))
                return false;

        uint base = childindex_base(parent_child_index);
        uint off = child_compact_offset(parent_mask, slot);
        child_index = base + off;
        return true;
}

Ray camera_ray_for_pixel(ShaderRayCam cam, uvec2 pixel, vec2 extent)
{
        Ray r;

        // origin packed in .w components
        r.origin = vec3(cam.u.w, cam.v.w, cam.w.w);

        // 1) pixel center sample
        vec2 uv = (vec2(pixel) + vec2(0.5)) / extent;

        // 2) map to [-1,1], with Y flipped
        float sx = 2.0 * uv.x - 1.0;
        float sy = 1.0 - 2.0 * uv.y;

        // 3) scale by half_w/half_h (precomputed from vfov + aspect)
        float half_w = cam.half_w_h.x;
        float half_h = cam.half_w_h.y;

        // 4) forward + sx*half_w*right + sy*half_h*up
        vec3 dir = cam.w.xyz
                        + sx * half_w * cam.u.xyz
                        + sy * half_h * cam.v.xyz;

        vec3 s = sign(dir);
        s = mix(vec3(1.0), s, greaterThan(abs(dir), vec3(0.0))); // if dir==0 -> +1

        dir = mix(dir, s * dirEps, lessThan(abs(dir), vec3(dirEps)));

        dir = normalize(dir);
        return r;
}

// ---------- Utilities ----------
uint get_local_index_from_vec3(ivec3 p) {
        // p.x,p.y,p.z expected in [0..3]
        return uint(p.x) | (uint(p.y) << 2u) | (uint(p.z) << 4u); // 0..63
}

// Branchless "min-axis" chooser like your step(side_dist.xxyy, side_dist.yzzx)
vec3 dda_cases_from_side_dist(vec3 side_dist) {
        // GLSL step(edge, x): returns 0 if x < edge, else 1

        // conds = step( xxyy, yzzx )
        float c0 = step(side_dist.x, side_dist.y); // y >= x
        float c1 = step(side_dist.x, side_dist.z); // z >= x
        float c2 = step(side_dist.y, side_dist.z); // z >= y
        float c3 = step(side_dist.y, side_dist.x); // x >= y

        vec3 cases = vec3(0.0);

        // x is smallest if x <= y and x <= z
        cases.x = c0 * c1;

        // y is smallest if not(x smallest) and y <= z and y <= x
        cases.y = (1.0 - cases.x) * c2 * c3;

        // otherwise z
        cases.z = 1.0 - cases.x - cases.y;

        return cases; // exactly one component should be 1.0
}

// ---------- “const_state” analog ----------
struct RayTraceConstState {
        ivec3 step_dir_i32; // sign(rayDir) per axis, typically -1 or +1 (0 allowed if rayDir==0)
        uint64_t occupancy_mask; // 64-bit occupancy for the current 4x4x4 cell
};

// ---------- BranchlessDDA ----------
struct BranchlessDDA {
        vec3 step_dir; // optional float version (not required if you keep step_dir_i32 in const_state)
        float voxel_size;

        vec3 side_dist; // tMax (distance to next grid boundary) in "t" units
        float t_tot; // total traveled t

        vec3 delta_dist; // tDelta per axis

        ivec3 local_pos; // integer voxel coord inside 4x4x4 (0..3)
        bvec3 prev_step; // optional: store axis as sign? (see below)
        ivec3 prev_step_i32;

        bool is_occupied;
};

void dda_init(
        inout BranchlessDDA d,
        ivec3 start_local_pos, // initial voxel coord (0..3)
        vec3 side_dist_init, // initial tMax per axis
        vec3 delta_dist_init, // tDelta per axis
        float voxel_size_
) {
        d.local_pos = start_local_pos;
        d.side_dist = side_dist_init;
        d.delta_dist = delta_dist_init;
        d.voxel_size = voxel_size_;
        d.t_tot = 0.0;
        d.prev_step_i32 = ivec3(0);
        d.out_of_bounds = false;
        d.is_occupied = false;
}

// This is your branchless step(), ported.
void dda_step(
        inout BranchlessDDA d,
        RayTraceConstState cs
) {
        // Choose which axis to step (branchless)
        vec3 cases = dda_cases_from_side_dist(d.side_dist);
        ivec3 cases_i32 = ivec3(cases); // (1,0,0) or (0,1,0) or (0,0,1)

        // Update voxel coordinate (integer stepping)
        d.local_pos += cs.step_dir_i32 * cases_i32;

        // Prev step (which direction we stepped)
        d.prev_step_i32 = -cs.step_dir_i32 * cases_i32;

        // Bounds check for only the stepped axis (your dot trick)
        int local_value = d.local_pos.x * cases_i32.x
                        + d.local_pos.y * cases_i32.y
                        + d.local_pos.z * cases_i32.z;

        d.out_of_bounds = (local_value > 3) || (local_value < 0);

        // Advance t and update side distances (branchless)
        float t_traveled = dot(d.side_dist, cases); // min component (selected axis)
        d.t_tot += t_traveled;

        // side_dist = side_dist - t_traveled + cases*delta_dist
        d.side_dist = d.side_dist - vec3(t_traveled) + cases * d.delta_dist;

        // Occupancy test at new position (if not out of bounds)
        // If you want fully branchless, you can still compute it and mask it out.
        uint idx = get_local_index_4x4x4(d.local_pos);
        bool occ = (((cs.occupancy_mask >> uint64_t(idx)) & 1ul) != 0ul);

        // If out_of_bounds, force false; this is still branchless (mix)
        d.is_occupied = mix(occ, false, d.out_of_bounds);
}
