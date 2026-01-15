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

layout(rgba8, set = 0, binding = BINDING_STORAGE_IMAGE) uniform image2D g_images[];


layout(push_constant) uniform constants {
  PushRay pc;
};

// Accessors (avoid repeating nonuniformEXT everywhere)
#define G_CAM(pc)          (g_cam[nonuniformEXT((pc).cam_id)].data)
#define G_NODE_MASK(pc,i)  (g_nodes[nonuniformEXT((pc).node_id)].data[(i)])
#define G_CHILD_PACK(pc,i) (g_child_indices[nonuniformEXT((pc).child_id)].data[(i)])
#define G_PAL(pc,i)        (g_palette[nonuniformEXT((pc).palette_id)].data[(i)])
#define G_LEAFMAT(pc,i)    (g_leaf_mats[nonuniformEXT((pc).leaf_mats_id)].data[(i)])
#define G_OUTIMG(pc)       (g_images[nonuniformEXT((pc).img_output_id)])

#endif
