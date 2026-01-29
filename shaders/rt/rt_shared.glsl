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

// ---------------------------
// ReadBackBufferStuff
// ---------------------------
#define DBG_MAX_RECORDS 16
#define DBG_T_U32    1
#define DBG_T_I32    2
#define DBG_T_F32    3
#define DBG_T_VEC2   4
#define DBG_T_VEC3   5
#define DBG_T_VEC4   6
#define DBG_T_IVEC2  7
#define DBG_T_IVEC3  8
#define DBG_T_UVEC2  9
#define DBG_T_UVEC3  10

// 1 Metadata uvec4 + N Record uvec4s
// Each uvec4 is 16 bytes.
#define DBG_PIXEL_STRIDE_BYTES (16 * (1 + DBG_MAX_RECORDS))

SHARED_STRUCT(DbgRecord, 16){
u32 header; // [Event:8][Key:8][Type:8][Part:8]
u32 y;
u32 z;
u32 w;
} ;

SHARED_STRUCT(DbgPixelBlock, 16){
u32 count;
u32 _pad[3];
DbgRecord records[DBG_MAX_RECORDS];
} ;

// Helpers to unpack header
#define DBG_GET_EVENT(h) ((h >> 24) & 0xFF)
#define DBG_GET_KEY(h)   ((h >> 16) & 0xFF)
#define DBG_GET_TYPE(h)  ((h >> 8) & 0xFF)
#define DBG_GET_PART(h)  ((h) & 0xFF)

#endif
