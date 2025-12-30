#version 460
#extension GL_EXT_nonuniform_qualifier : require

// --- Bindless Resources ---
// Binding 1 defined in resmanager.c as "Storage Buffers"
layout(set = 0, binding = 1) readonly buffer Verts {
        vec3 data[];
} all_buffers[];

// --- Push Constants ---
layout(push_constant) uniform PC {
        uint vbo_idx; // Resource ID of the vertex buffer
} pc;

layout(location = 0) out vec3 outColor;

void main() {
        // 1. Pull Vertex Data manually using the ID and Vertex Index
        // non-uniform index because vbo_idx changes per draw call
        vec3 pos = all_buffers[nonuniformEXT(pc.vbo_idx)].data[gl_VertexIndex];

        gl_Position = vec4(pos, 1.0);

        // Debug color based on position
        outColor = pos + 0.5;
}

