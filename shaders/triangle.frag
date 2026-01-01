#version 460
#extension GL_ARB_shading_language_include : require


layout(location = 0) in vec3 inColor;
layout(location = 0) out vec4 outFragColor;

void main() {
        outFragColor = vec4(inColor.rgb, 1.0);
}
