#ifdef __STDC__
#pragma once
#endif

#include "raytrace.glsl"

bool childindex_is_leaf(uint packed_first_child_index)
{
        return (packed_first_child_index & LEAF_BIT) != 0u;
}

uint childindex_base(uint packed_first_child_index)
{
        return (packed_first_child_index & INDEX_MASK);
}

bool ray_aabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float t0, out float t1)
{
        vec3 inv = 1.0 / rd;
        vec3 tbot = (bmin - ro) * inv;
        vec3 ttop = (bmax - ro) * inv;
        vec3 tmin = min(tbot, ttop);
        vec3 tmax = max(tbot, ttop);
        t0 = max(max(tmin.x, tmin.y), tmin.z);
        t1 = min(min(tmax.x, tmax.y), tmax.z);
        return t1 >= max(t0, 0.0);
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
bool node_get_child_index(
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

Ray camera_ray_uvww(
        vec3 cam_pos,
        vec3 u_right,
        vec3 v_up,
        vec3 w_fwd,
        float fovY_deg,
        uvec2 pixel,
        vec2 extent)
{
        // pixel center in [0..1]
        vec2 uv = (vec2(pixel) + vec2(0.5)) / extent;

        // NDC [-1..1], Vulkan-style Y flip (top-left origin image)
        float sx = uv.x * 2.0 - 1.0;
        float sy = 1.0 - uv.y * 2.0;

        float aspect = extent.x / max(1.0, extent.y);

        float tan_half_fovy = tan(0.5 * radians(fovY_deg));
        float tan_half_fovx = tan_half_fovy * aspect;

        vec3 dir =
                normalize(
                        w_fwd
                                + sx * tan_half_fovx * u_right
                                + sy * tan_half_fovy * v_up
                );

        // optional: clamp near-zero components to avoid DDA inv-dir explosions
        const float dirEps = 1e-8;
        vec3 s = sign(dir);
        s = mix(vec3(1.0), s, greaterThan(abs(dir), vec3(0.0)));
        dir = mix(dir, s * dirEps, lessThan(abs(dir), vec3(dirEps)));
        dir = normalize(dir);

        Ray r;
        r.origin = cam_pos;
        r.dir = dir;
        return r;
}

Ray camera_ray_for_pixel(ShaderRayCam cam, uvec2 pixel, vec2 extent)
{
        Ray r;

        // origin packed in .w components
        r.origin = vec3(cam.u[3], cam.v[3], cam.w[3]);

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

        const float dirEps = 1e-8;
        dir = mix(dir, s * dirEps, lessThan(abs(dir), vec3(dirEps)));

        r.dir = normalize(dir);
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
        bool out_of_bounds;
};

bool point_inside_aabb(vec3 p, vec3 bmin, vec3 bmax)
{
        // Inclusive bounds (inside if on the faces too)
        return all(greaterThanEqual(p, bmin)) && all(lessThan(p, bmax));
}

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
bool init_node_dda(
        Ray ray,
        vec3 node_min,
        float node_size,
        float t_base,
        out BranchlessDDA dda_out)
{
        float cell = node_size * 0.25;

        // eps relative to node size (prevents boundary sticking at all depths)
        float eps = max(1e-6, 1e-6 * node_size);

        vec3 node_max = node_min + vec3(node_size);

        // sample point slightly inside along the ray
        float t = t_base + eps;
        vec3 p_world = ray.origin + t * ray.dir;

        // half-open bounds: [min, max)
        // if (!point_inside_aabb(p_world, node_min, node_max))
        //         return false;

        // local position, clamped strictly inside node to avoid floor()==4
        vec3 p_local = p_world - node_min;
        p_local = clamp(p_local, vec3(0.0), vec3(node_size - eps));

        ivec3 cell_pos = ivec3(floor(p_local / cell));
        cell_pos = clamp(cell_pos, ivec3(0), ivec3(3));

        ivec3 step_dir = ivec3(sign(ray.dir));

        const float inf = 3.402823e38;
        vec3 inv_dir = 1.0 / ray.dir;
        inv_dir = mix(inv_dir, vec3(inf), lessThan(abs(ray.dir), vec3(1e-12)));

        // next boundary in local space
        // if dir>0 -> (cell_pos+1)*cell
        // if dir<0 -> cell_pos*cell
        vec3 next_boundary = vec3(cell_pos) * cell + step(vec3(0.0), ray.dir) * cell;

        vec3 side_dist = (next_boundary - p_local) * inv_dir;
        vec3 delta_dist = abs(inv_dir) * cell;

        // axes with step_dir==0 never cross boundaries
        side_dist = mix(side_dist, vec3(inf), equal(step_dir, ivec3(0)));
        delta_dist = mix(delta_dist, vec3(inf), equal(step_dir, ivec3(0)));

        // IMPORTANT: avoid negative side_dist due to precision / boundary cases
        side_dist = max(side_dist, vec3(0.0));

        dda_init(dda_out, cell_pos, side_dist, delta_dist, cell);
        return true;
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
        uint idx = get_local_index_from_vec3(d.local_pos);
        bool occ = (((cs.occupancy_mask >> uint64_t(idx)) & 1ul) != 0ul);

        // If out_of_bounds, force false; this is still branchless (mix)
        d.is_occupied = mix(occ, false, d.out_of_bounds);
}

struct TraverseFrame {
        uint node_index;
        float t_base;
        uint level;
        vec3 node_min;
        float node_size;
        BranchlessDDA dda;
        uint steps_in_node;
};

const uint TRACE_OK = 0u;
const uint TRACE_ERR_ORIGIN_OUTSIDE = 1u;
const uint TRACE_ERR_DIR_ZERO = 2u;
const uint TRACE_ERR_DIR_NAN_INF = 3u;
const uint TRACE_ERR_INIT_NODE_FAIL = 4u;
const uint TRACE_ERR_DDA_NAN_INF = 5u;
const uint TRACE_ERR_STACK_OVERFLOW = 6u;
const uint TRACE_ERR_CHILD_SELF_LOOP = 7u;
const uint TRACE_ERR_CHILD_INDEX_OOB = 8u;
const uint TRACE_ERR_MAX_ITER = 9u;
const uint TRACE_ERR_LEVEL_OOB = 10u;
const uint TRACE_ERR_DDA_STUCK = 123u;
const uint TRACE_ERR_NODE_STEP_CAP = 124u;

t pvec4 trace_error_color ( uint err )
{
// TRACE_OK should normally not be shown in the "errors" view
if ( err == TRACE_OK ) return vec4(0.0, 0.0, 0.0, 1.0); // black

if ( err == TRACE_ERR_ORIGIN_OUTSIDE ) return vec4(1.0, 0.0, 1.0, 1.0); // magenta
if ( err == TRACE_ERR_DIR_ZERO ) return vec4(1.0, 1.0, 0.0, 1.0); // yellow
if ( err == TRACE_ERR_DIR_NAN_INF ) return vec4(0.0, 1.0, 1.0, 1.0); // cyan
if ( err == TRACE_ERR_INIT_NODE_FAIL ) return vec4(1.0, 0.5, 0.0, 1.0); // orange
if ( err == TRACE_ERR_DDA_NAN_INF ) return vec4(0.0, 1.0, 0.0, 1.0); // green
if ( err == TRACE_ERR_STACK_OVERFLOW ) return vec4(1.0, 0.0, 0.0, 1.0); // red
if ( err == TRACE_ERR_CHILD_SELF_LOOP ) return vec4(0.6, 0.0, 1.0, 1.0); // purple
if ( err == TRACE_ERR_CHILD_INDEX_OOB ) return vec4(0.2, 0.2, 1.0, 1.0); // blue
if ( err == TRACE_ERR_MAX_ITER ) return vec4(1.0, 1.0, 1.0, 1.0); // white
if ( err == TRACE_ERR_LEVEL_OOB ) return vec4(0.3, 0.3, 0.3, 1.0); // gray
if ( err == TRACE_ERR_DDA_STUCK ) return vec4(1.0, 0.0, 0.5, 1.0); // hot pink
if ( err == TRACE_ERR_NODE_STEP_CAP ) return vec4(0.0, 0.0, 0.5, 1.0); // dark blue

// Unknown error code
return vec4(1.0, 0.0, 0.0, 1.0); // black
}
