#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#define CLAY_IMPLEMENTATION

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_GLFW_VULKAN_IMPLEMENTATION
#include "thirdparty/nuklear/nuklear_glfw_vulkan.in.h"

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
#include "sample_interface.h"
#include "sample_runner.h"
#include "submit_manager.h"
#include "system_manager.h"

// --- Private Prototypes ---
static void _register_systems(GPUSystemInfo info);

int main() {
  // 1. Init Windowp
  u32 width = 800;
  u32 height = 600;

  if (!glfwInit())
    return -1;

  GPUSystemInfo gpu_info = {.info = (GPUInstanceInfo){.app_name = "RenderGraph Demo", .enable_validation = true}};

  _register_systems(gpu_info);

  if (!glslang_initialize_process()) {
    printf("Failed to initialize glslang process.\n");
    exit(1);
  }

  // Sample sample = create_triangle_sample();
  Sample sample = create_raytrace_sample();
  run_sample(&sample);

  glfwTerminate();
  glslang_finalize_process();
  return 0;
}
// --- Private Functions ---

static void _register_systems(GPUSystemInfo info) {
  m_system_register(gpu_system_get_func(), SYSTEM_TYPE_GPU, &info);
  m_system_register(rm_system_get_func(), SYSTEM_TYPE_RESOURCE, NULL);
  m_system_register(fm_system_get_func(), SYSTEM_TYPE_FILE, NULL);
  m_system_register(pm_system_get_func(), SYSTEM_TYPE_PIPELINE, NULL);
  m_system_register(pr_system_get_func(), SYSTEM_TYPE_HOTRELOAD, NULL);

  m_system_init();
}
