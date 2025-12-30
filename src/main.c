#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/shader_compiler.h"
#include "resmanager.h"

void glslang_compile_test(GPUDevice device) {
  const char *path = "shaders/triangle.frag";
  compile_glsl_to_spirv(device.device, path, SHADER_STAGE_FRAGMENT);
}

int main() {
  // 1. Init Windowp
  LOG_INFO("test");
  LOG_WARN("test");
  LOG_ERROR("test");
  if (!glfwInit())
    return -1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window =
      glfwCreateWindow(800, 600, "RenderGraph Demo", NULL, NULL);

  // 2. Init GPU
  GPUDevice device;
  if (!gpu_init(&device, window,
                &(GPUInstanceInfo){.enable_validation = true})) {
    printf("GPU Init Failed\n");
    return 1;
  }

  // 3. Init Swapchain
  GPUSwapchain swapchain;
  gpu_swapchain_init(&device, &swapchain, 800, 600);

  if (!glslang_initialize_process()) {
    printf("Failed to initialize glslang process.\n");
    exit(1);
  }

  glslang_compile_test(device);

  return 1;

  // 4. Init Managers
  ResourceManager *rm; // TODO, fix later
  rm_init(&device);

  // -------------------------------------

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (gpu_swapchain_acquire(&device, &swapchain)) {
    }
  }

  vkDeviceWaitIdle(device.device);

  rm_destroy(rm);
  gpu_swapchain_destroy(&device, &swapchain);
  gpu_destroy(&device);

  glfwDestroyWindow(window);
  glfwTerminate();
  glslang_finalize_process();
  return 0;
}
