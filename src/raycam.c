
// camera.h
#pragma once
#include "GLFW/glfw3.h"
#include "cglm/util.h"
#include "common.h"
#include <X11/X.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "gpu/gpu.h"
#include "raycam.h"
#include "system_manager.h"

#include "shaders/rt/rt_shared.glsl"
#include "util.h"

typedef struct {
  int key;
  uint32_t mode;
  const char *name;
} DebugModeKey;

static const DebugModeKey kDebugKeys[] = {
    {GLFW_KEY_F1, DEBUG_MODE_HIT, "HIT"},
    {GLFW_KEY_F2, DEBUG_MODE_ITER_GRAY, "ITER_GRAY"},
    {GLFW_KEY_F3, DEBUG_MODE_LEVEL, "LEVEL"},
    {GLFW_KEY_F4, DEBUG_MODE_ITER_GRAY_HIT_GREEN, "ITER_GRAY_HIT_GREEN"},
    {GLFW_KEY_F5, DEBUG_MODE_ERRORS, "ERRORS"},
    {GLFW_KEY_F6, DEBUG_MODE_NORMAL, "NORMAL"},
    {GLFW_KEY_F7, DEBUG_MODE_OCCULUSION, "OCCLUSION"},
};

static int s_key_was_down[GLFW_KEY_LAST + 1];

// --- Private Prototypes ---
static void set_debug_mode(Camera *cam, GLFWwindow *window, uint32_t mode, const char *name);
static void handle_debug_keys(Camera *cam, GLFWwindow *window);
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

static int first_mouse = 1;

void camera_update(Camera *cam, GLFWwindow *window, double delta) {
  const float pitch_limit = 1.553343f; // radians (89 degrees)

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

  const float sensitivity = 0.002f;

  if (cursor_captured == true) {
    // 1) Update pitch (clamped)
    cam->yaw += dx * sensitivity;
    cam->pitch += dy * sensitivity;
  }

  _update_basis(cam);
}

// --- Private Functions ---

static void _update_basis(Camera *cam) {
  const float pitch_limit = 1.553343f;
  cam->pitch = glm_clamp(cam->pitch, -pitch_limit, pitch_limit);

  float cy = cosf(cam->yaw);
  float sy = sinf(cam->yaw);
  float cp = cosf(cam->pitch);
  float sp = sinf(cam->pitch);

  // forward (looking direction). Here: yaw=0 looks down -Z.
  cam->w[0] = sy * cp;
  cam->w[1] = sp;
  cam->w[2] = -cy * cp;
  glm_vec3_normalize(cam->w);

  vec3 world_up = {0, 1, 0};

  // right = forward x world_up  (right-handed with forward=-Z at yaw=0)
  glm_vec3_cross(cam->w, world_up, cam->u);
  glm_vec3_normalize(cam->u);

  // up = right x forward
  glm_vec3_cross(cam->u, cam->w, cam->v);
  glm_vec3_normalize(cam->v);
}

static void _move(Camera *cam, GLFWwindow *window, float dt) {
  float speed = 5.0f * dt;
  float speed_modifer = 1;

  if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
    speed_modifer = 5;
  }

  float new_speed = speed_modifer * speed;

  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
    glm_vec3_muladds(cam->w, new_speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
    glm_vec3_muladds(cam->w, -new_speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
    glm_vec3_muladds(cam->u, -new_speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
    glm_vec3_muladds(cam->u, new_speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
    glm_vec3_muladds(cam->v, -new_speed, cam->pos);
  }
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
    glm_vec3_muladds(cam->v, new_speed, cam->pos);
  }

  int esc_down = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
  if (esc_down && !s_key_was_down[GLFW_KEY_ESCAPE])
    toggle_cursor(window);

  s_key_was_down[GLFW_KEY_ESCAPE] = esc_down;

  handle_debug_keys(cam, window);
}

static void toggle_cursor(GLFWwindow *window) {
  double now = glfwGetTime();
  if (now - last_toggle_time < 0.20)
    return;
  last_toggle_time = now;

  cursor_captured = !cursor_captured;
  glfwSetInputMode(window, GLFW_CURSOR, cursor_captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

  // prevent camera jump on next mouse read
  first_mouse = 1;

  if (cursor_captured) {
    int w, h;
    glfwGetWindowSize(window, &w, &h);
    glfwSetCursorPos(window, w * 0.5, h * 0.5);
  }
}

static void set_debug_mode(Camera *cam, GLFWwindow *window, uint32_t mode, const char *name) {
  if (cam->debug_mode == mode)
    return;
  cam->debug_mode = mode;

  // Pick one (or keep both):
  LOG_INFO("Debug mode: %s (%u)\n", name, mode);

  char title[128];
  snprintf(title, sizeof(title), "%s", name);
  glfwSetWindowTitle(window, title);
}

static void handle_debug_keys(Camera *cam, GLFWwindow *window) {
  for (size_t i = 0; i < sizeof(kDebugKeys) / sizeof(kDebugKeys[0]); ++i) {
    int key = kDebugKeys[i].key;
    int down = (glfwGetKey(window, key) == GLFW_PRESS);

    if (down && !s_key_was_down[key]) { // rising edge
      set_debug_mode(cam, window, kDebugKeys[i].mode, kDebugKeys[i].name);
    }
    s_key_was_down[key] = down;
  }
}
