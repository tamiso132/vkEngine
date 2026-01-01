#include "raytrace_sample.h"
#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES

#include "util.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/swapchain.h"
#include "resmanager.h"
#include "sample_interface.h"
#include "triangle_sample.h"

int main() {
  // 1. Init Windowp

  u32 width = 800;
  u32 height = 600;

  if (!glfwInit())
    return -1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window = glfwCreateWindow(width, height, "RenderGraph Demo", NULL, NULL);

  // 2. Init GPU
  GPUDevice device;
  if (!gpu_init(&device, window, &(GPUInstanceInfo){.enable_validation = true})) {
    printf("GPU Init Failed\n");
    return 1;
  }

  // 3. Init Swapchain

  if (!glslang_initialize_process()) {
    printf("Failed to initialize glslang process.\n");
    exit(1);
  }

  Managers mg = {};
  init_managers(&mg, &device);

  GPUSwapchain swapchain;
  swapchain_init(&device, mg.rm, &swapchain, &width, &height);
  // Sample sample = create_triangle_sample();
  Sample sample = create_raytrace_sample();
  run_sample(&sample, &mg, &device, window, &swapchain);

  vkDeviceWaitIdle(device.device);

  rm_destroy(mg.rm);
  swapchain_destroy(&device, &swapchain);
  gpu_destroy(&device);

  glfwDestroyWindow(window);
  glfwTerminate();
  glslang_finalize_process();
  return 0;
}
// --- Private Functions ---
