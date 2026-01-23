#include "rt_bindings.glsl"
#include "rt_shared.glsl"
#include "rt_util.glsl"

vec3 snap_anchor(vec3 p_local, vec3 n_face)
{
        // push slightly outward so anchor is on the empty side
        vec3 p = p_local + n_face * 1e-3;

        // snap to voxel center for stability
        ivec3 v = clamp(ivec3(floor(p)), ivec3(0), ivec3(CHUNK_SIZE - 1));
        return vec3(v) + vec3(0.5);
}
bool in_bounds_i(ivec3 p)
{
        return all(greaterThanEqual(p, ivec3(0))) &&
                all(lessThan(p, ivec3(CHUNK_SIZE)));
}

bool occ_leaf_voxel(uvec3 v)
{
        if (any(greaterThanEqual(v, uvec3(CHUNK_SIZE))))
                return false;

        uint node = 0u;

        for (uint level = 0u; level < uint(TREE_LEVELS); ++level)
        {
                uint64_t mask = G_NODE_MASK(pc, node);
                uint packed = G_CHILD_PACK(pc, node);

                uint slot = child_slot_from_xyz(v, level, uint(TREE_LEVELS));
                bool occ = child_present(mask, slot);

                bool is_leaf_level = (level == uint(TREE_LEVELS) - 1u);
                bool early_leaf = childindex_is_leaf(packed);

                if (is_leaf_level || early_leaf)
                        return occ;

                if (!occ)
                        return false;

                uint child;
                if (!node_get_child_index(mask, packed, slot, child))
                        return false;

                node = child;
        }

        return false;
}

float occ_soft_oob_empty(vec3 p_local)
{
        // If the base cell would require corners outside, treat as empty
        // (You can also only early-out on p_local itself, but this is cleaner.)
        if (any(lessThan(p_local, vec3(0.0))) ||
                        any(greaterThanEqual(p_local, vec3(float(CHUNK_SIZE) - 1.0))))
                return 0.0;

        ivec3 b = ivec3(floor(p_local));
        vec3 f = p_local - vec3(b);

        // 8 corners; if OOB => 0
        float c000 = in_bounds_i(b + ivec3(0, 0, 0)) ? (occ_leaf_voxel(uvec3(b + ivec3(0, 0, 0))) ? 1.0 : 0.0) : 0.0;
        float c100 = in_bounds_i(b + ivec3(1, 0, 0)) ? (occ_leaf_voxel(uvec3(b + ivec3(1, 0, 0))) ? 1.0 : 0.0) : 0.0;
        float c010 = in_bounds_i(b + ivec3(0, 1, 0)) ? (occ_leaf_voxel(uvec3(b + ivec3(0, 1, 0))) ? 1.0 : 0.0) : 0.0;
        float c110 = in_bounds_i(b + ivec3(1, 1, 0)) ? (occ_leaf_voxel(uvec3(b + ivec3(1, 1, 0))) ? 1.0 : 0.0) : 0.0;
        float c001 = in_bounds_i(b + ivec3(0, 0, 1)) ? (occ_leaf_voxel(uvec3(b + ivec3(0, 0, 1))) ? 1.0 : 0.0) : 0.0;
        float c101 = in_bounds_i(b + ivec3(1, 0, 1)) ? (occ_leaf_voxel(uvec3(b + ivec3(1, 0, 1))) ? 1.0 : 0.0) : 0.0;
        float c011 = in_bounds_i(b + ivec3(0, 1, 1)) ? (occ_leaf_voxel(uvec3(b + ivec3(0, 1, 1))) ? 1.0 : 0.0) : 0.0;
        float c111 = in_bounds_i(b + ivec3(1, 1, 1)) ? (occ_leaf_voxel(uvec3(b + ivec3(1, 1, 1))) ? 1.0 : 0.0) : 0.0;

        float c00 = mix(c000, c100, f.x);
        float c10 = mix(c010, c110, f.x);
        float c01 = mix(c001, c101, f.x);
        float c11 = mix(c011, c111, f.x);

        float c0 = mix(c00, c10, f.y);
        float c1 = mix(c01, c11, f.y);

        return mix(c0, c1, f.z);
}

float occ_soft(vec3 p_local) // p_local in chunk-local voxel space (0..CHUNK_SIZE)
{
        // clamp inside so floor+1 stays valid
        p_local = clamp(p_local, vec3(0.0), vec3(float(CHUNK_SIZE) - 1.001));

        ivec3 b = ivec3(floor(p_local));
        vec3 f = p_local - vec3(b);

        // 8 corners
        float c000 = occ_leaf_voxel(uvec3(b + ivec3(0, 0, 0))) ? 1.0 : 0.0;
        float c100 = occ_leaf_voxel(uvec3(b + ivec3(1, 0, 0))) ? 1.0 : 0.0;
        float c010 = occ_leaf_voxel(uvec3(b + ivec3(0, 1, 0))) ? 1.0 : 0.0;
        float c110 = occ_leaf_voxel(uvec3(b + ivec3(1, 1, 0))) ? 1.0 : 0.0;
        float c001 = occ_leaf_voxel(uvec3(b + ivec3(0, 0, 1))) ? 1.0 : 0.0;
        float c101 = occ_leaf_voxel(uvec3(b + ivec3(1, 0, 1))) ? 1.0 : 0.0;
        float c011 = occ_leaf_voxel(uvec3(b + ivec3(0, 1, 1))) ? 1.0 : 0.0;
        float c111 = occ_leaf_voxel(uvec3(b + ivec3(1, 1, 1))) ? 1.0 : 0.0;

        float c00 = mix(c000, c100, f.x);
        float c10 = mix(c010, c110, f.x);
        float c01 = mix(c001, c101, f.x);
        float c11 = mix(c011, c111, f.x);

        float c0 = mix(c00, c10, f.y);
        float c1 = mix(c01, c11, f.y);

        return mix(c0, c1, f.z);
}

float neighborhood_ao_soft(vec3 p0_local, vec3 n)
{
        // p0_local: chunk-local continuous position near the surface
        // n: unit normal (use normalize(vec3(cur.dda.prev_step_i32)))

        const ivec3 O[18] = ivec3[18](
                        // axis 6
                        ivec3(1, 0, 0), ivec3(-1, 0, 0),
                        ivec3(0, 1, 0), ivec3(0, -1, 0),
                        ivec3(0, 0, 1), ivec3(0, 0, -1),

                        ivec3(1, 1, 0), ivec3(1, -1, 0), ivec3(-1, 1, 0), ivec3(-1, -1, 0),
                        ivec3(1, 0, 1), ivec3(1, 0, -1), ivec3(-1, 0, 1), ivec3(-1, 0, -1),
                        ivec3(0, 1, 1), ivec3(0, 1, -1), ivec3(0, -1, 1), ivec3(0, -1, -1)
                );

        float occ = 0.0;
        float wsum = 0.0;

        for (int i = 0; i < 18; ++i)
        {
                vec3 o = vec3(O[i]);
                float d2 = dot(o, o);
                vec3 dir = o * inversesqrt(d2);

                // hemisphere gate
                float h = max(0.0, dot(n, dir));
                h = sqrt(h);
                if (h <= 0.0) continue;

                // thinner falloff: (1+d^2)^-2  (use ^-3 if you want even tighter)
                float t = 1.0 + d2;
                float wd = 1.0 / (t * t);

                float w = h * wd;

                // continuous occupancy sample
                float filled = occ_soft(p0_local + o);

                occ += filled * w;
                wsum += w;
        }

        float ao = 1.0;
        if (wsum > 1e-6) ao = 1.0 - (occ / wsum);

        ao = clamp(ao, 0.0, 1.0);

        float ao_strength = 0.2; // 0..1 (lower = lighter)
        float ao_floor = 0.60; // minimum light

        ao = mix(1.0, ao, ao_strength);
        ao = max(ao, ao_floor);

        // "thinness"/contrast curve: <1 makes it lighter/thinner, >1 darker/fatter
        ao = pow(ao, 0.7);

        return ao;
}

float neighborhood_ao(uvec3 v, vec3 n)
{
        const ivec3 O[14] = ivec3[14](
                        ivec3(1, 0, 0), ivec3(-1, 0, 0),
                        ivec3(0, 1, 0), ivec3(0, -1, 0),
                        ivec3(0, 0, 1), ivec3(0, 0, -1),
                        ivec3(1, 1, 0), ivec3(1, -1, 0),
                        ivec3(-1, 1, 0), ivec3(-1, -1, 0),
                        ivec3(1, 0, 1), ivec3(-1, 0, 1),
                        ivec3(0, 1, 1), ivec3(0, -1, 1)
                );

        float occ = 0.0;
        float wsum = 0.0;

        for (int i = 0; i < 14; ++i)
        {
                vec3 o = vec3(O[i]);
                float d2 = dot(o, o);
                vec3 dir = o * inversesqrt(d2);

                float h = max(0.0, dot(n, dir));
                if (h <= 0.0) continue;

                // thinner falloff
                float t = 1.0 + d2;
                float wd = 1.0 / (t * t); // <-- steeper than before

                float w = h * wd;

                ivec3 vi = ivec3(v) + O[i];
                if (any(lessThan(vi, ivec3(0))) || any(greaterThanEqual(vi, ivec3(CHUNK_SIZE))))
                        continue;

                //    occ += (occ_leaf_voxel(uvec3(vi)) ? 1.0 : 0.0) * w;

                float filled = occ_soft(v + o);
                occ += filled * w;

                wsum += w;
        }

        float ao = 1.0;
        if (wsum > 1e-6) ao = 1.0 - occ / wsum;

        ao = clamp(ao, 0.0, 1.0);

        // less “fat” contrast
        ao = pow(ao, 1.2);

        // overall strength control (thinner)
        float strength = 0.55;
        ao = mix(1.0, ao, strength);

        return ao;
}

void ao_and_normal_soft(
        vec3 p0_local,
        vec3 n_hint, // can be face normal from dda (used only for hemisphere gating)
        out float ao,
        out vec3 n_smooth)
{
        const ivec3 O[18] = ivec3[18](
                        ivec3(1, 0, 0), ivec3(-1, 0, 0),
                        ivec3(0, 1, 0), ivec3(0, -1, 0),
                        ivec3(0, 0, 1), ivec3(0, 0, -1),

                        ivec3(1, 1, 0), ivec3(1, -1, 0), ivec3(-1, 1, 0), ivec3(-1, -1, 0),
                        ivec3(1, 0, 1), ivec3(1, 0, -1), ivec3(-1, 0, 1), ivec3(-1, 0, -1),
                        ivec3(0, 1, 1), ivec3(0, 1, -1), ivec3(0, -1, 1), ivec3(0, -1, -1)
                );

        float occ = 0.0;
        float wsum = 0.0;

        // gradient accumulator for smooth normal
        vec3 g = vec3(0.0);
        float gsum = 0.0;

        for (int i = 0; i < 18; ++i)
        {
                vec3 o = vec3(O[i]);
                float d2 = dot(o, o);
                vec3 dir = o * inversesqrt(d2);

                // hemisphere gate (use hint; keeps AO from sampling "behind" surface)
                float h = dot(n_hint, dir);
                if (h <= 0.0) continue;
                h = sqrt(h);

                float t = 1.0 + d2;
                float wd = 1.0 / (t * t); // thin falloff

                float w = h * wd;

                float filled = occ_soft_oob_empty(p0_local + o);

                occ += filled * w;
                wsum += w;

                // Use same sample to estimate gradient of the density field
                g += filled * dir * w;
                gsum += w;
        }

        // AO
        ao = 1.0;
        if (wsum > 1e-6) ao = 1.0 - (occ / wsum);
        ao = clamp(ao, 0.0, 1.0);

        // Your remap (keep)
        float ao_strength = 0.2;
        float ao_floor = 0.60;
        ao = mix(1.0, ao, ao_strength);
        ao = max(ao, ao_floor);
        ao = pow(ao, 0.7);

        // Smooth normal from gradient (outward = -grad)
        if (gsum > 1e-6) g /= gsum;
        if (dot(g, g) < 1e-8) {
                n_smooth = normalize(n_hint); // fallback
        } else {
                n_smooth = normalize(-g);
        }
}

void ao_and_normal_soft_stable(
        vec3 p_local, // (hit_pos - chunk_min)
        vec3 n_face, // face normal from DDA (only used for hemisphere gate)
        out float ao,
        out vec3 n_smooth)
{
        const ivec3 O[18] = ivec3[18](
                        ivec3(1, 0, 0), ivec3(-1, 0, 0),
                        ivec3(0, 1, 0), ivec3(0, -1, 0),
                        ivec3(0, 0, 1), ivec3(0, 0, -1),

                        ivec3(1, 1, 0), ivec3(1, -1, 0), ivec3(-1, 1, 0), ivec3(-1, -1, 0),
                        ivec3(1, 0, 1), ivec3(1, 0, -1), ivec3(-1, 0, 1), ivec3(-1, 0, -1),
                        ivec3(0, 1, 1), ivec3(0, 1, -1), ivec3(0, -1, 1), ivec3(0, -1, -1)
                );

        vec3 p0 = snap_anchor(p_local, n_face);

        float occ = 0.0;
        float wsum = 0.0;

        vec3 g = vec3(0.0);
        float gsum = 0.0;

        for (int i = 0; i < 18; ++i)
        {
                vec3 o = vec3(O[i]);
                float d2 = dot(o, o);
                vec3 dir = o * inversesqrt(d2);

                // hemisphere gate (stable because n_face is stable)
                float h = dot(n_face, dir);
                if (h <= 0.0) continue;
                h = sqrt(h);

                float t = 1.0 + d2;
                float wd = 1.0 / (t * t); // thin falloff
                float w = h * wd;

                float filled = occ_soft(p0 + o);

                occ += filled * w;
                wsum += w;

                // shared sample contributes to gradient too
                g += filled * dir * w;
                gsum += w;
        }

        // AO
        ao = 1.0;
        if (wsum > 1e-6) ao = 1.0 - occ / wsum;
        ao = clamp(ao, 0.0, 1.0);

        // remap (your knobs)
        ao = pow(ao, 0.7);
        ao = mix(1.0, ao, 0.2);
        ao = max(ao, 0.60);

        // Smooth normal from shared samples
        if (gsum > 1e-6) g /= gsum;
        if (dot(g, g) < 1e-8) n_smooth = n_face;
        else n_smooth = normalize(-g);

        // optional: keep it from drifting too far from face normal
        // n_smooth = normalize(mix(n_face, n_smooth, 0.85));
}
