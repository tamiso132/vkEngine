#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES

#include "common.h"
#include "filewatch.h"
#include "gpu/pipeline.h"
#include "raytrace_sample.h"
#include "submit_manager.h"

#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "resmanager.h"
#include "sample_interface.h"
#include "sample_runner.h"
#include "submit_manager.h"
#include "system_manager.h"

// --- Private Prototypes ---
static void _register_systems(GPUSystemInfo info);

#include "chunk.h"

int main() {
  // 1. Init Windowp
  return chunk_test();
  u32 width = 800;
  u32 height = 600;

  if (!glfwInit())
    return -1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window = glfwCreateWindow(width, height, "RenderGraph Demo", NULL, NULL);

  GPUSystemInfo gpu_info = {.window = window,
                            .info = (GPUInstanceInfo){.app_name = "RenderGraph Demo", .enable_validation = true}};

  _register_systems(gpu_info);

  if (!glslang_initialize_process()) {
    printf("Failed to initialize glslang process.\n");
    exit(1);
  }

  // Sample sample = create_triangle_sample();
  Sample sample = create_raytrace_sample();
  run_sample(&sample, window);

  glfwDestroyWindow(window);
  glfwTerminate();
  glslang_finalize_process();
  return 0;
}
// --- Private Functions ---

static void _register_systems(GPUSystemInfo info) {
  m_system_register(gpu_system_get_func(), SYSTEM_TYPE_GPU, &info);
  m_system_register(rm_system_get_func(), SYSTEM_TYPE_RESOURCE, NULL);
  m_system_register(swapchain_system_get_func(), SYSTEM_TYPE_SWAPCHAIN, NULL);
  m_system_register(fm_system_get_func(), SYSTEM_TYPE_FILE, NULL);
  m_system_register(pm_system_get_func(), SYSTEM_TYPE_PIPELINE, NULL);
  m_system_register(pr_system_get_func(), SYSTEM_TYPE_HOTRELOAD, NULL);
  m_system_register(sm_system_get_func(), SYSTEM_TYPE_SUBMIT, NULL);

  m_system_init();
}
