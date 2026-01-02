#include "common.h"
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
#include "system_manager.h"
#include "triangle_sample.h"
int main() {
  // 1. Init Windowp

  u32 width = 800;
  u32 height = 600;

  if (!glfwInit())
    return -1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window = glfwCreateWindow(width, height, "RenderGraph Demo", NULL, NULL);

  GPUSystemInfo gpu_info = {.window = window,
                            .info = (GPUInstanceInfo){.app_name = "RenderGraph Demo", .enable_validation = true}};

  m_system_register(gpu_system_get_func(), SYSTEM_TYPE_GPU, &gpu_info);

  ResourceManager *manager = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, ResourceManager);
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

  _destroy(mg.rm);
  swapchain_destroy(&device, &swapchain);
  gpu_destroy(&device);

  glfwDestroyWindow(window);
  glfwTerminate();
  glslang_finalize_process();
  return 0;
}
// --- Private Functions ---
