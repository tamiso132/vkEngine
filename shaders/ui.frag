#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#include "ui_shared.glsl"

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = BINDING_SAMPLED_IMAGE) uniform sampler2D textures[];

void main() {
        vec4 tex = texture(textures[nonuniformEXT(pc.tex_id)], fragUV);
        outColor = fragColor * tex;
}
