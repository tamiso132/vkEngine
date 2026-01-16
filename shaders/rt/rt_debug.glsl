#ifndef RT_DEBUG_GLSL
#define RT_DEBUG_GLSL

#include "rt_shared.glsl"


vec4 debug_view_color(
  uint debug_mode,
  bool hit,
  uint iter_count,
  uint max_iter,
  uint hit_level,
  uint hit_slot,
  uint err_code,
  vec3 hit_color, 
  vec3 n_smooth,
  float ao)
{
  if (debug_mode == DEBUG_MODE_HIT) {
    return hit ? vec4(hit_color, 1.0) : vec4(0.0, 0.5, 0.0, 1.0);
  }
  if (debug_mode == DEBUG_MODE_ITER_GRAY) {
    float t = clamp(float(iter_count) / max(1.0, float(max_iter)), 0.0, 1.0);
    return vec4(vec3(t), 1.0);
  }
  if (debug_mode == DEBUG_MODE_LEVEL) {
    float t = hit ? (float(hit_level) / max(1.0, float(TREE_LEVELS - 1))) : 0.0;
    return vec4(vec3(t), 1.0);
  }
  if (debug_mode == DEBUG_MODE_ITER_GRAY_HIT_GREEN) {
    float t = clamp(float(iter_count) / max(1.0, float(max_iter)), 0.0, 1.0);
    return vec4(vec3(t), 1.0);
  }
  if (debug_mode == DEBUG_MODE_ERRORS) {
    return trace_error_color(err_code);
  }

  if(debug_mode == DEBUG_MODE_NORMAL){
     vec3 nn = normalize(n_smooth);
   return vec4(nn * 0.5 + 0.5, 1.0);
  }

  if(debug_mode == DEBUG_MODE_OCCULUSION){
    return vec4(ao, ao, ao, 1.0);
  }
  return vec4(0, 0, 0, 1);
}

#endif
