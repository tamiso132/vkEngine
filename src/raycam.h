#pragma once

#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

typedef struct Camera {
  vec3 pos;

  // Orthonormal basis (world-space)
  vec3 w; // points where camera looks
  vec3 u;
  vec3 v;

  // Projection params
  float vfov_deg;
  float aspect; // width / height

  float yaw;   // x-y
  float pitch; // y-z

} Camera;
// PUBLIC FUNCTIONS

void camera_update(Camera *cam, GLFWwindow *window, double delta);

Camera camera_init();
