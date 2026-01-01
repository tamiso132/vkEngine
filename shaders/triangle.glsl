#ifdef __STDC__
#pragma once
#endif

#include "shader_base.glsl"

SHARED_STRUCT(PushTriangle, 16){
u32 vertex_id;
} ;

SHARED_STRUCT(PushComputeTriangle, 16){
vec4 data;
vec2 extent;
u32 img_id;
} ;
