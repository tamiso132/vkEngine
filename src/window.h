#pragma once
#include "common.h"
#include "gpu/swapchain.h"
#include <GLFW/glfw3.h>

typedef struct TWindow {
  GLFWwindow *raw_window;
  VkSurfaceKHR surface;
  M_Swapchain swapchain;
  bool is_focused;
  int width, height;
} TWindow;

static inline void window_init(TWindow *win, M_GPU *gpu, M_Resource *rm, int w, int h, const char *title,
                               const char *swapchain_name) {
  win->width = w;
  win->height = h;

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  win->raw_window = glfwCreateWindow(w, h, title, NULL, NULL);
  glfwCreateWindowSurface(gpu->instance, win->raw_window, NULL, &win->surface);

  // Initialize the embedded swapchain
  swapchain_init(&win->swapchain, win->surface, swapchain_name);
}

static inline void window_destroy(TWindow *win, M_GPU *gpu) {
  swapchain_destroy(&win->swapchain);
  vkDestroySurfaceKHR(gpu->instance, win->surface, NULL);
  glfwDestroyWindow(win->raw_window);
}
