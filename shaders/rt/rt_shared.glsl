#ifndef RT_TYPES_H
#define RT_TYPES_H

#include "../shader_base.glsl"

SHARED_CONST_U32(DEBUG_MODE_HIT, 0);
SHARED_CONST_U32(DEBUG_MODE_ITER_GRAY, 1);
SHARED_CONST_U32(DEBUG_MODE_LEVEL, 2);
SHARED_CONST_U32(DEBUG_MODE_ITER_GRAY_HIT_GREEN, 3);
SHARED_CONST_U32(DEBUG_MODE_ERRORS, 4);
SHARED_CONST_U32(DEBUG_MODE_NORMAL, 5);
SHARED_CONST_U32(DEBUG_MODE_OCCULUSION, 6);

#define TREE_LEVELS 4
#define BITS_PER_AXIS_PER_LEVEL 2
#define AXIS_COUNT 3
#define BITS_PER_LEVEL (BITS_PER_AXIS_PER_LEVEL * AXIS_COUNT) /* 6 */
#define BITS_PER_AXIS (BITS_PER_AXIS_PER_LEVEL * TREE_LEVELS) /* 2L */
#define CHUNK_SIZE (1 << BITS_PER_AXIS)                       /* 4^L */
#define UNDEFINED_VALUE 0xFFFFFFFFu
//Optimizations
#define OPTIMIZATION_RT_JUMP_EMPTY_SPACE 1

// voxel counts
#define VOXELS_PER_WORD 64
#define VOXELS_PER_CHUNK (1ULL << (BITS_PER_LEVEL * TREE_LEVELS))
#define WORDS_PER_CHUNK (VOXELS_PER_CHUNK / VOXELS_PER_WORD)

// traversal / indexing helpers
#define LEVEL_SHIFT(d) ((d) * BITS_PER_LEVEL)
#define CHILD_SLOT(morton, d) (((morton) >> LEVEL_SHIFT(d)) & 63ULL)

#define BIT_MASK_U64(bit)   (1ULL << ((bit) & 63))

SHARED_STRUCT(ShaderRayCam, 16){
vec4 u;
vec4 v;
vec4 w;
vec2 half_w_h;
} ;

SHARED_STRUCT(GPUGridSlot, 16){
u32 nodes_id;
u32 child_index_id;
u32 palette_id;
u32 leaf_mat_id;
} ;

SHARED_STRUCT(PushRay, 16){
u32 cam_id;
u32 img_output_id;
vec2 extent;
u32 debug_mode;
u32 grid_id;
vec3 grid_min_corner;
u32 readback_idx;
ivec2 mouse_px;
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
#ifndef DEBUG_SHARED_H
#define DEBUG_SHARED_H


// Total uint words a single pixel can store per frame
SHARED_CONST_U32(DBG_MAX_WORDS,  64);


// --- DBG Type IDs (1 byte) ---
SHARED_CONST_U32(DBG_T_U32,  1);
SHARED_CONST_U32(DBG_T_I32,  2);
SHARED_CONST_U32(DBG_T_F32,  3);
SHARED_CONST_U32(DBG_T_VEC2, 4);
SHARED_CONST_U32(DBG_T_VEC3, 5);
SHARED_CONST_U32(DBG_T_VEC4, 6);

// --- DBG Event Tags (1 byte) ---
SHARED_CONST_U32(DBG_EV_GENERIC,  0);
SHARED_CONST_U32(DBG_EV_INIT,     1);
SHARED_CONST_U32(DBG_EV_BOUNCE,   2);
SHARED_CONST_U32(DBG_EV_LIGHTING, 3);

// --- DBG Data Keys (1 byte) ---
SHARED_CONST_U32(DBG_KEY_NONE,   0);
SHARED_CONST_U32(DBG_KEY_POS,    1);
SHARED_CONST_U32(DBG_KEY_NORM,   2);
SHARED_CONST_U32(DBG_KEY_ALBEDO, 3);
SHARED_CONST_U32(DBG_KEY_DEPTH,  4);

#endif
#endif
