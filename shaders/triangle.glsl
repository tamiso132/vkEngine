#include "shader_base.glsl"

typedef struct ALIGN ( 16 ) PushTriangle {
        u32 vertex_id;
} PushTriangle;

typedef struct ALIGN ( 16 ) PushComputeTriangle {
        u32 vbo_idx;
        vec2 extent;
} PushComputeTriangle;

void testtest() {}
