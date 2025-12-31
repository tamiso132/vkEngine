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
#include "gpu/swapchain.h"
#include "resmanager.h"
#include "submit_manager.h"
#include "triangle.h"

typedef struct {
  float x, y, z;
  float padding; // Mandatory: aligns the struct to 16 bytes
} Vertex;

typedef struct {
  ResourceManager *rm;
  M_SubmitManager *sm;
  M_Pipeline *pm;
  M_PipelineReloader *reloader;
  FileManager *fm;
} Managers;

// --- Private Prototypes ---

// 2. Define the data
const Vertex triangleVertices[] = {
    {0.0f, -0.5f, 0.0f, 0.0f}, // Top
    {0.5f, 0.5f, 0.0f, 0.0f},  // Bottom Right
    {-0.5f, 0.5f, 0.0f, 0.0f}  // Bottom Left
};

void init_managers(Managers *mg, GPUDevice *device) {
  mg->rm = rm_init(device);
  mg->pm = pm_init(mg->rm);
  mg->fm = fm_init();
  mg->reloader = pr_init(mg->pm, mg->fm);
  mg->sm = submit_manager_create(device->device, device->graphics_queue, 1);
}

void glslang_compile_test(GPUDevice device) {
  const char *path = "shaders/triangle.frag";
  FileManager *fm = fm_init();
  FileGroup *fg = fg_init(fm);

  CompileResult result = {.fg = fg, .include_dir = str_get_dir(path), .shader_path = path};

  shader_compile_glsl(device.device, &result, SHADER_STAGE_FRAGMENT);
}

void triangle_hotreload_test(Managers *mg, GLFWwindow *window, GPUSwapchain *swapchain) {

  GPUDevice *device = rm_get_gpu(mg->rm);

  const char *vs_path = "shaders/triangle.vert";
  const char *fs_path = "shaders/triangle.frag";

  VkFormat color_format = swapchain->format;

  GpBuilder b = gp_init(mg->rm, "TrianglePipeline");
  gp_set_topology(&b, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  gp_set_cull(&b, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  gp_set_color_formats(&b, &color_format, 1);

  PipelineHandle triangle_pip = pr_build_reg(mg->reloader, &b, vs_path, fs_path);

  CmdBuffer cmd = cmd_init(device->device, device->graphics_family);

  RGBufferInfo buffer_info = {.capacity = sizeof(Vertex) * 12,
                              .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                              .name = "TriangleVertices",
                              .usage = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT};

  ResHandle vertex_handle = rm_create_buffer(mg->rm, &buffer_info);

  cmd_begin(device->device, cmd);

  rm_buffer_upload(mg->rm, cmd.buffer, vertex_handle, (void *)triangleVertices, sizeof(Vertex) * 12);

  cmd_end(device->device, cmd);

  VkSubmitInfo submitInfo = {
      .commandBufferCount = 1, .pCommandBuffers = &cmd.buffer, .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO};

  vkQueueSubmit(device->graphics_queue, 1, &submitInfo, NULL);
  vkQueueWaitIdle(device->graphics_queue);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    pr_update_modifed(mg->reloader);
    submit_begin_frame(mg->sm);
    submit_acquire_swapchain(mg->sm, swapchain);

    cmd_begin(device->device, cmd);
    cmd_bind_bindless(cmd, mg->rm, swapchain->extent);
    ResHandle swap_img = swapchain_get_image(swapchain);
    ImageBarrierInfo color_barrier_info = {.img_handle = swap_img,
                                           .src_access = VK_ACCESS_NONE,
                                           .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                           .src_layout = VK_IMAGE_LAYOUT_UNDEFINED,

                                           .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                           .dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                           .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};

    rm_image_sync(mg->rm, cmd.buffer, &color_barrier_info);

    RenderingBeginInfo begin_info = {.colors = &swap_img,
                                     .colors_count = 1,
                                     .h = swapchain->extent.height,
                                     .w = swapchain->extent.width,
                                     .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                     .clear_color = {1.0, 0.0, 1.0}};

    cmd_begin_rendering(cmd, mg->rm, &begin_info);

    PushTriangle push = {.vertex_id = rm_get_buffer_descriptor_index(mg->rm, vertex_handle)};
    BindPipelineInfo bind_info = {.handle = triangle_pip, .p_push = &push, .push_size = sizeof(PushTriangle)};

    cmd_bind_pipeline(cmd, mg->pm, &bind_info);

    vkCmdDraw(cmd.buffer, 3, 1, 0, 0);

    cmd_end_rendering(cmd);

    ImageBarrierInfo present_barrier_info = {.img_handle = swap_img,
                                             .src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                             .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                             .src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

                                             .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                             .dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                             .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};

    rm_image_sync(mg->rm, cmd.buffer, &present_barrier_info);
    cmd_end(device->device, cmd);

    submit_work(mg->sm, swapchain, cmd.buffer, true, true);

    submit_present(mg->sm, swapchain);
  }
}

int main() {
  // 1. Init Windowp

  if (!glfwInit())
    return -1;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow *window = glfwCreateWindow(800, 600, "RenderGraph Demo", NULL, NULL);

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
  swapchain_init(&device, mg.rm, &swapchain, 800, 600);

  triangle_hotreload_test(&mg, window, &swapchain);

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
