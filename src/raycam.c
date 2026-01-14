
// camera.h
#pragma once
#include "GLFW/glfw3.h"
#include "common.h"
#include <X11/X.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "gpu/gpu.h"
#include "raycam.h"
#include "system_manager.h"

#include "shaders/raytrace.glsl"

// --- Private Prototypes ---
static void _update_basis(Camera *cam);
static void _move(Camera *cam, GLFWwindow *window, float dt);
static void toggle_cursor(GLFWwindow *window);

static int cursor_captured = 0;
static double last_toggle_time = 0.0;

Camera camera_init(VkExtent2D extent, float fovy) {
  auto cam = (Camera){
      .aspect = (float)extent.width / extent.height, .vfov_deg = fovy, .u = {1, 0, 0}, .w = {0, 0, 1}, .v = {0, 1, 0}};
  _update_basis(&cam);
  return cam;
}

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
  if (fabsf(glm_vec3_dot(cam->w, world_up)) > 0.999f) {
    world_up[0] = 0;
    world_up[1] = 0;
    world_up[2] = 1;
  }
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
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    toggle_cursor(window);
  }

  if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS)
    cam->debug_mode = DEBUG_MODE_HIT;

  if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS)
    cam->debug_mode = DEBUG_MODE_ITER_GRAY;

  if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS)
    cam->debug_mode = DEBUG_MODE_LEVEL;

  if (glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS)
    cam->debug_mode = DEBUG_MODE_ITER_GRAY_HIT_GREEN;

  if (glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS)
    cam->debug_mode = DEBUG_MODE_ERRORS;
}

static void toggle_cursor(GLFWwindow *window) {
  cursor_captured = !cursor_captured;
  double now = glfwGetTime();
  if (now - last_toggle_time < 0.20)
    return; // 200ms debounce

  last_toggle_time = now;
  if (cursor_captured) {
    // Cursor hidden + captured to window, relative mouse movement
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  } else {
    // Cursor visible + free
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
}
