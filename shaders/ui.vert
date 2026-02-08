#version 460

#extension GL_ARB_shading_language_include : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ui_shared.glsl"

layout(push_constant) uniform PushConstants {
        GPUPushUI pc;
};

layout(set = 0, binding = BINDING_STORAGE_BUFFER) readonly buffer VtxBuffers {
        GPUNuklearVertex data[];
} vtx_buffers[];

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
        GPUNuklearVertex v = vtx_buffers[nonuniformEXT(pc.vert_id)].data[gl_VertexIndex];

        vec2 ndc = (v.pos / pc.screen_size) * 2.0 - 1.0;
        gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);

        fragUV = v.uv;
        fragColor = vec4(v.color) / 255.0;
}
