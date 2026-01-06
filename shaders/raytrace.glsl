#ifdef __STDC__
#pragma once
#endif

#include "shader_base.glsl"

SHARED_STRUCT(ShaderRayCam, 16){
vec4 u;
vec4 v;
vec4 w;
vec2 half_w_h;
vec2 extent;
} ;

// Ray camera_ray_for_pixel(const Camera *cam, int px, int py, int image_w, int image_h) {
//   Ray ray;
//   glm_vec3_copy(cam->pos, ray.origin);
//
//   // 1) Pixel center sampling (avoids bias):
//   // u, v in [0,1]
//   float u = ((float)px + 0.5f) / (float)image_w;
//   float v = ((float)py + 0.5f) / (float)image_h;
//
//   // 2) Map to [-1,1] screen space
//   // x: left=-1 right=+1
//   // y: top=+1 bottom=-1  (note the flip)
//   float sx = 2.0f * u - 1.0f;
//   float sy = 1.0f - 2.0f * v;
//
//   // 3) Convert to camera plane scale using vertical FOV
//   float half_h = tanf(deg_to_rad(cam->vfov_deg) * 0.5f);
//   float half_w = cam->aspect * half_h;
//
//   // 4) Direction = forward + sx*half_w*right + sy*half_h*up
//   vec3 dir;
//   vec3 tmp;
//
//   glm_vec3_copy((float *)cam->forward, dir);
//
//   glm_vec3_scale((float *)cam->right, sx * half_w, tmp);
//   glm_vec3_add(dir, tmp, dir);
//
//   glm_vec3_scale((float *)cam->up, sy * half_h, tmp);
//   glm_vec3_add(dir, tmp, dir);
//
//   glm_vec3_normalize_to(dir, ray.dir);
//   return ray;
// }
