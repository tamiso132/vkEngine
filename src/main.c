#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES

#include "filewatch.h"
#include "gpu/pipeline.h"
#include "util.h"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/shader_compiler.h"
#include "resmanager.h"
#include "submit_manager.h"

typedef struct {
  float x, y, z;
  float padding; // Mandatory: aligns the struct to 16 bytes
} Vertex;

// 2. Define the data
const Vertex triangleVertices[] = {
    {0.0f, -0.5f, 0.0f, 0.0f}, // Top
    {0.5f, 0.5f, 0.0f, 0.0f},  // Bottom Right
    {-0.5f, 0.5f, 0.0f, 0.0f}  // Bottom Left
};

void glslang_compile_test(GPUDevice device) {
  const char *path = "shaders/triangle.frag";
  FileManager *fm = fm_init();
  FileGroup *fg = fg_init(fm);

  CompileResult result = {
      .fg = fg, .include_dir = str_get_dir(path), .shader_path = path};

  shader_compile_glsl(device.device, &result, SHADER_STAGE_FRAGMENT);
}

void triangle_hotreload_test(ResourceManager *rm, GLFWwindow *window,
                             GPUSwapchain *swapchain) {
  const char *vs_path = "shaders/triangle.vert";
  const char *fs_path = "shaders/triangle.frag";

  GPUDevice *device = rm_get_gpu(rm);

  M_Pipeline *pm = pm_init(rm);

  FileManager *fm = fm_init();
  M_PipelineReloader *reloader = pr_init(pm, fm);

  VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;

  GpBuilder b = gp_init(rm, "TrianglePipeline");
  gp_set_topology(&b, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gp_set_cull(&b, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gp_set_color_formats(&b, &color_format, 1);

  PipelineHandle pip = pr_build_reg(reloader, &b, vs_path, fs_path);

  SubmitManager sm =
      submit_manager_create(device->device, device->graphics_queue, 1);

  CmdBuffer cmd = cmd_init(device->device, device->graphics_family);

  RGBufferInfo buffer_info = {.capacity = sizeof(Vertex) * 12,
                              .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                              .name = "TriangleVertices",
                              .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT};

  ResHandle vertex_handle = rm_create_buffer(rm, &buffer_info);

  cmd_begin(device->device, cmd);

  rm_buffer_upload(rm, cmd.buffer, vertex_handle, (void *)triangleVertices,
                   sizeof(Vertex) * 12);

  cmd_end(device->device, cmd);

  VkSubmitInfo submitInfo = {.commandBufferCount = 1,
                             .pCommandBuffers = &cmd.buffer,
                             .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};

  vkQueueSubmit(device->graphics_queue, 1, &submitInfo, NULL);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    // if (gpu_swapchain_acquire(device, swapchain)) {
    //  submit_begin_frame(sm);
    // }
  }
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

  triangle_hotreload_test(rm, window, &swapchain);

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
