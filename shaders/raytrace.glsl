#ifdef __STDC__
#pragma once
#endif

#include "shader_base.glsl"

#define TREE_LEVELS 3u
#define BITS_PER_AXIS_PER_LEVEL 2u
#define AXIS_COUNT 3u
#define BITS_PER_LEVEL (BITS_PER_AXIS_PER_LEVEL * AXIS_COUNT) // 6
#define BITS_PER_AXIS (BITS_PER_AXIS_PER_LEVEL * TREE_LEVELS) // 2L
#define CHUNK_SIZE (1u << BITS_PER_AXIS)                      // 4^L

#define VOXELS_PER_WORD 64ul
#define VOXELS_PER_CHUNK (1ul << (BITS_PER_LEVEL * TREE_LEVELS)) // 2^(6L)
#define WORDS_PER_CHUNK (VOXELS_PER_CHUNK / VOXELS_PER_WORD)

// ===== traversal / indexing helpers =====
#define LEVEL_SHIFT(d) (uint(d) * uint(BITS_PER_LEVEL))
#define CHILD_SLOT(morton, d) uint(((morton) >> LEVEL_SHIFT(d)) & 63ul)

#define BITSET_WORD(morton) ((morton) >> 6)
#define BITSET_BIT(morton)  ((morton) & 63ul)
#define BIT_MASK_U64(bit)   (1ul << ((bit) & 63u))

SHARED_STRUCT(ShaderRayCam, 16){
vec4 u;
vec4 v;
vec4 w;
vec2 half_w_h;
} ;

SHARED_STRUCT(PushRay, 16){
u32 cam_id;
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
const uint LEAF_BIT = 0x80000000u;
const uint INDEX_MASK = 0x7FFFFFFFu;
