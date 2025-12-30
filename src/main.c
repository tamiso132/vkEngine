#include "filewatch.h"
#include "gpu/pipeline.h"
#include "util.h"
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/shader_compiler.h"
#include "resmanager.h"

void glslang_compile_test(GPUDevice device) {
  const char *path = "shaders/triangle.frag";
  FileManager *fm = fm_init();
  FileGroup *fg = fg_init(fm);

  CompileResult result = {
      .fg = fg, .include_dir = str_get_dir(path), .shader_path = path};

  shader_compile_glsl(device.device, &result, SHADER_STAGE_FRAGMENT);
}

void hotreload(ResourceManager *rm) {
  const char *vs_path = "shaders/triangle.vert";
  const char *fs_path = "shaders/triangle.frag";

  M_Pipeline *pm = pm_init(rm);

  FileManager *fm = fm_init();
  M_PipelineReloader *reloader = pr_init(pm, fm);

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

  GpBuilder b = gp_init(rm, "TrianglePipeline");
  gp_set_topology(&b, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gp_set_cull(&b, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gp_set_color_formats(&b, &color_format, 1);

  PipelineHandle pip = pr_build_reg(reloader, &b, vs_path, fs_path);
}
int main() {
  // 1. Init Windowp

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
  ResourceManager *rm = rm_init(&device);
  hotreload(rm);
  return 1;

  // 4. Init Managers
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
