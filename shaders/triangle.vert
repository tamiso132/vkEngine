#version 460
#extension GL_ARB_shading_language_include : require
#extension GL_EXT_nonuniform_qualifier : require

#include "shader_base.ini"
// --- Bindless Resources ---
// Binding 1 defined in resmanager.c as "Storage Buffers"
layout(set = 0, binding = BINDING_STORAGE_BUFFER) readonly buffer Verts {
        vec3 data[];
} all_buffers[];

// --- Push Constants ---
layout(push_constant) uniform PC {
        uint vbo_idx; // Resource ID of the vertex buffer
} pc;

layout(location = 0) out vec3 outColor;

void main() {
        vec3 pos = all_buffers[nonuniformEXT(pc.vbo_idx)].data[gl_VertexIndex];
        gl_Position = vec4(pos, 1.0);
        outColor = pos + 0.5;
}
