#version 460
#extension GL_ARB_shading_language_include : require
#extension GL_EXT_nonuniform_qualifier : require

struct GPUPushUI{
vec2 screen_size;
uint tex_id; // If using bindless
uint vert_id;
};

layout(push_constant) uniform constants {
        GPUPushUI pc;
};

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D textures[];

void main() {
        vec4 tex = texture(textures[nonuniformEXT(pc.tex_id)], fragUV);
        outColor = fragColor * tex;
}
