#version 460

#extension GL_EXT_nonuniform_qualifier : enable

#include "ui_shared.glsl"

layout(set = 0, binding = BINDING_STORAGE_BUFFER) readonly buffer VtxBuffers {
        GPUClayVertex data[];
} vtx_buffers[];

out vec2 fragUV;
out vec4 fragColor;

void main() {
        ClayVertex v = vtx_buffers[nonuniformEXT(vtx_buf_id)].vertices[gl_VertexIndex];

        vec2 ndc = (v.pos / screen_size) * 2.0 - 1.0;
        gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);

        fragUV = v.uv;
        fragColor = v.color;
}
