#ifndef RT_BINDINGS_GLSL
#define RT_BINDINGS_GLSL

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#include "rt_shared.glsl"

// Buffers / images
layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer CamBuf {
        ShaderRayCam data;
} g_cam[];

layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer NodesBuf {
        uint64_t data[];
} g_nodes[];

layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer ChildIdxBuf {
        uint data[];
} g_child_indices[];

layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer PaletteColor {
        u32 data[];
} g_palette[];

layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer LeafMatBuf {
        uint data[];
} g_leaf_mats[];

layout(set = 0, binding = BINDING_STORAGE_BUFFER, std430) readonly buffer ChunkIndices {
        GPUGridSlot data[];
} g_grid_indices[];

layout(rgba8, set = 0, binding = BINDING_STORAGE_IMAGE) uniform image2D g_images[];

layout(push_constant) uniform constants {
        PushRay pc;
};

#define G_CAM(pc)          (g_cam[nonuniformEXT((pc).cam_id)].data)
#define G_GRID(pc, i)          (g_grid_indices[nonuniformEXT((pc).grid_id)].data[i])
#define G_NODE_MASK(gs, i)  (g_nodes[nonuniformEXT((gs).nodes_id)].data[(i)])
#define G_CHILD_PACK(gs, i) (g_child_indices[nonuniformEXT((gs).child_index_id)].data[(i)])
#define G_PAL(gs, i)        (g_palette[nonuniformEXT((gs).palette_id)].data[(i)])
#define G_LEAFMAT(gs, i)    (g_leaf_mats[nonuniformEXT((gs).leaf_mat_id)].data[(i)])
#define G_OUTIMG(pc)       (g_images[nonuniformEXT((pc).img_output_id)])

#define GET_LEAF_MATERIAL(gs, idx) ((G_LEAFMAT(gs, (idx) >> 1u) >> (((idx) & 1u) << 4u)) & 0xFFFFu)


#endif
