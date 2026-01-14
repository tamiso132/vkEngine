#pragma once

#include "common.h"
#include "volk.h"
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

typedef struct Camera {
  vec3 pos;

  // Orthonormal basis (world-space)
  vec3 w; // points where camera looks
  vec3 u;
  vec3 v;

  // Projection params
  f32 vfov_deg;
  f32 aspect; // width / height

  f32 yaw;   // x-y
  f32 pitch; // y-z
             //
  u32 debug_mode;

} Camera;
// PUBLIC FUNCTIONS

void camera_update(Camera *cam, GLFWwindow *window, double delta);

Camera camera_init(VkExtent2D extent, float fovy);
