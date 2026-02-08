#include "shader_base.glsl"

SHARED_STRUCT(GPUPushUI, 16){
vec2 screen_size;
uint tex_id; // If using bindless
uint vert_id;
mat4 projection;
} ;

SHARED_STRUCT(GPUNuklearVertex, 16){
vec2 pos;
vec2 uv;
u32 color;
} ;
