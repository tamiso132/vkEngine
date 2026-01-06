
// camera.h
#pragma once
#include "common.h"
#include <X11/X.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "gpu/gpu.h"
#include "raycam.h"
#include "system_manager.h"

// --- Private Prototypes ---
static void _update_basis(Camera *cam);
static void _move(Camera *cam, GLFWwindow *window, float dt);

Camera camera_init() { return (Camera){}; }

static double last_x, last_y;
static int first_mouse = 1;

void camera_update(Camera *cam, GLFWwindow *window, double delta) {

  _move(cam, window, delta);
  static double last_x = 0;
  static double last_y = 0;

  double x, y;
  glfwGetCursorPos(window, &x, &y);

  if (first_mouse) {
    last_x = x;
    last_y = y;
    first_mouse = 0;
  }

  float dx = (float)(x - last_x);
  float dy = (float)(last_y - y); // inverted Y

  last_x = x;
  last_y = y;

  float sensitivity = 0.002f;
  cam->yaw += dx * sensitivity;
  cam->pitch += dy * sensitivity;

  _update_basis(cam);
}

// --- Private Functions ---

static void _update_basis(Camera *cam) {
  // Clamp pitch to avoid flipping
  if (cam->pitch > 1.55f)
    cam->pitch = 1.55f;
  if (cam->pitch < -1.55f)
    cam->pitch = -1.55f;
  // Forward from yaw/pitch
  // Right-handed, -Z forward when yaw=pitch=0
  cam->w[0] = cosf(cam->pitch) * sinf(cam->yaw);
  cam->w[1] = sinf(cam->pitch);
  cam->w[2] = -cosf(cam->pitch) * cosf(cam->yaw);
  glm_vec3_normalize(cam->w);

  vec3 world_up = {0, 1, 0};

  glm_vec3_cross(cam->w, world_up, cam->u);
  glm_vec3_normalize(cam->u);

  glm_vec3_cross(cam->u, cam->w, cam->v);
  glm_vec3_normalize(cam->v);
}

static void _move(Camera *cam, GLFWwindow *window, float dt) {
  float speed = 5.0f * dt;

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    glm_vec3_muladds(cam->w, speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    glm_vec3_muladds(cam->w, -speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    glm_vec3_muladds(cam->u, -speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    glm_vec3_muladds(cam->u, speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
    glm_vec3_muladds(cam->v, -speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
    glm_vec3_muladds(cam->v, speed, cam->pos);
  }
}
