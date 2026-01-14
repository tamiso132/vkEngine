#ifdef __STDC__
#pragma once
#endif

#include "shader_base.glsl"

#define TREE_LEVELS 3
#define BITS_PER_AXIS_PER_LEVEL 2
#define AXIS_COUNT 3
#define BITS_PER_LEVEL (BITS_PER_AXIS_PER_LEVEL * AXIS_COUNT) /* 6 */
#define BITS_PER_AXIS (BITS_PER_AXIS_PER_LEVEL * TREE_LEVELS) /* 2L */
#define CHUNK_SIZE (1 << BITS_PER_AXIS)                       /* 4^L */

// voxel counts
#define VOXELS_PER_WORD 64
#define VOXELS_PER_CHUNK (1ULL << (BITS_PER_LEVEL * TREE_LEVELS)) /* 2^(6L) */
#define WORDS_PER_CHUNK (VOXELS_PER_CHUNK / VOXELS_PER_WORD)

// traversal / indexing helpers
#define LEVEL_SHIFT(d) ((d) * BITS_PER_LEVEL)
#define CHILD_SLOT(morton, d) (((morton) >> LEVEL_SHIFT(d)) & 63ULL)

#define BITSET_WORD(morton) ((morton) >> 6)
#define BITSET_BIT(morton)  ((morton) & 63ULL)
#define BIT_MASK_U64(bit)   (1ULL << ((bit) & 63))

SHARED_STRUCT(ShaderRayCam, 16){
vec4 u;
vec4 v;
vec4 w;
vec2 half_w_h;
} ;

SHARED_STRUCT(PushRay, 16){
u32 cam_id;
u32 node_id;
u32 child_id;
u32 img_output_id;
vec2 extent;
} ;

struct Ray {
        vec3 origin;
        vec3 dir;
};

// ---------------------------
// Bit packing for child index
// ---------------------------
#define LEAF_BIT  0x80000000u
#define INDEX_MASK 0x7FFFFFFFu
