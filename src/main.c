#define GLFW_INCLUDE_VULKAN
#define VK_NO_PROTOTYPES
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glslang/Include/glslang_c_interface.h>

#include "gpu/gpu.h"
#include "gpu/pipeline.h"
#include "rendergraph.h"
#include "resmanager.h"

// --- User Data for the Pass ---
typedef struct {
  RGHandle vbo;
  RGHandle output_tex;
  VkExtent2D extent;
  ResourceManager *rm;
  GPUPipeline pipeline;
} TriangleData;

// --- Execution Callback ---
static void draw_triangle(VkCommandBuffer cmd, void *user_data) {
  TriangleData *data = (TriangleData *)user_data;

  // 1. Get Resources (This will now return the *current* swapchain view)
  VkImageView view = rm_get_image_view(data->rm, data->output_tex);
  uint32_t vbo_id = data->vbo.id;

  // 2. Begin Dynamic Rendering
  VkRenderingAttachmentInfo color_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {{{0.05f, 0.05f, 0.05f, 1.0f}}}};

  VkRenderingInfo render_info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                 .renderArea = {{0, 0}, data->extent},
                                 .layerCount = 1,
                                 .colorAttachmentCount = 1,
                                 .pColorAttachments = &color_att};

  vkCmdBeginRendering(cmd, &render_info);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    data->pipeline.pipeline);

  VkDescriptorSet global_set = rm_get_bindless_set(data->rm);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          data->pipeline.layout, 0, 1, &global_set, 0, NULL);

  VkViewport vp = {
      0, 0, (float)data->extent.width, (float)data->extent.height, 0.0f, 1.0f};
  vkCmdSetViewport(cmd, 0, 1, &vp);
  VkRect2D sc = {{0, 0}, data->extent};
  vkCmdSetScissor(cmd, 0, 1, &sc);

  vkCmdPushConstants(cmd, data->pipeline.layout, VK_SHADER_STAGE_ALL, 0,
                     sizeof(uint32_t), &vbo_id);

  vkCmdDraw(cmd, 3, 1, 0, 0);
  vkCmdEndRendering(cmd);
}

int main() {
  // 1. Init Window
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

  // 4. Init Managers
  ResourceManager rm;
  rm_init(&rm, &device);

  RenderGraph *rg = rg_init(&rm);

  // 5. Create Persistent Resources
  float vertices[] = {
      0.0f,  -0.5f, 0.0f, // Top
      0.5f,  0.5f,  0.0f, // Right
      -0.5f, 0.5f,  0.0f  // Left
  };

  RGHandle vbo = rm_create_buffer(&rm, "TriangleVBO", sizeof(vertices),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT);

  // 6. Pipeline Setup
  GpBuilder b = gp_init();
  gp_set_shaders(&b, "shaders/triangle.vert", "shaders/triangle.frag");
  VkFormat surf_fmt = VK_FORMAT_B8G8R8A8_SRGB;
  gp_set_color_formats(&b, &surf_fmt, 1);
  gp_set_layout(&b, rm_get_bindless_layout(&rm), sizeof(uint32_t));

  GPUPipeline g_triangle_pipe = gp_build(device.device, &b);
  gp_register_hotreload(device.device, &g_triangle_pipe, &b);

  // --- RENDER GRAPH SETUP (Run Once) ---

  // A. Create a STABLE handle for the backbuffer.
  // We import VK_NULL_HANDLE initially; we will hot-swap this pointer every
  // frame.

  RGHandle h_backbuffer = rm_create_image(
      &rm, (RGImageInfo){.name = "SceneColor",
                         .width = 800,
                         .height = 600,
                         .usage = VK_IMAGE_USAGE_STORAGE_BIT |
                                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                         .format = VK_FORMAT_R8G8B8_UNORM,
                         .preset = RG_IMAGETYPE_ATTACHMENT});

  // B. Setup Pass Data (Persistent struct)
  TriangleData pass_data = {
      .rm = &rm,
      .vbo = vbo,
      .output_tex = h_backbuffer,
      .extent = {swapchain.extent.width, swapchain.extent.height},
      .pipeline = g_triangle_pipe};

  // C. Build Graph (Nodes & Edges)
  RGPass *p = rg_add_pass(rg, "Draw Triangle", RG_TASK_GRAPHIC, draw_triangle,
                          &pass_data);

  rg_pass_read(p, vbo, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
  rg_pass_set_color_target(p, h_backbuffer, (VkClearValue){{0, 0, 0, 1}});

  rg_compile(rg);

  // -------------------------------------

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (gpu_swapchain_acquire(&device, &swapchain)) {
      // 1. Hot-Swap the Backbuffer Resource
      // We update the underlying VkImage/View for the stable 'h_backbuffer'
      // handle.
      RGResource *res = &rm.resources[h_backbuffer.id];
      res->img.img.vk = swapchain.images[swapchain.current_img_idx];
      res->img.img.view = swapchain.views[swapchain.current_img_idx];

      // CRITICAL: Reset layout state to UNDEFINED.
      // The RenderGraph thinks the resource is in "ColorAttachment" state from
      // the previous frame. But this is a NEW swapchain image that is likely
      // UNDEFINED or PRESENT_SRC. Resetting this ensures RG injects the correct
      // "Undefined -> ColorAttach" barrier.
      res->img.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

      // 2. Update dynamic data (like window size)
      pass_data.extent =
          (VkExtent2D){swapchain.extent.width, swapchain.extent.height};
      pass_data.pipeline = g_triangle_pipe; // Update in case of hot-reload

      // 3. Execute (Reuses the baked graph)
      VkCommandBuffer cmd = device.imm_cmd_buffer;
      vkResetCommandBuffer(cmd, 0);
      VkCommandBufferBeginInfo bi = {
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      vkBeginCommandBuffer(cmd, &bi);

      rg_execute(rg, cmd);

      // 4. Manual Barrier for Presentation
      VkImageMemoryBarrier2 to_present = {
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
          .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
          .dstAccessMask = 0,
          .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .image = swapchain.images[swapchain.current_img_idx],
          .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

      VkDependencyInfo dep = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                              .imageMemoryBarrierCount = 1,
                              .pImageMemoryBarriers = &to_present};
      vkCmdPipelineBarrier2(cmd, &dep);

      vkEndCommandBuffer(cmd);

      // 5. Submit & Present
      VkPipelineStageFlags wait_stage =
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .waitSemaphoreCount = 1,
                         .pWaitSemaphores = &swapchain.acquire_sem,
                         .pWaitDstStageMask = &wait_stage,
                         .commandBufferCount = 1,
                         .pCommandBuffers = &cmd,
                         .signalSemaphoreCount = 1,
                         .pSignalSemaphores = &swapchain.present_sem};

      vkQueueSubmit(device.graphics_queue, 1, &si, swapchain.frame_fence);
      gpu_swapchain_present(&device, &swapchain, device.graphics_queue);
    }
  }

  vkDeviceWaitIdle(device.device);

  rg_destroy(rg);
  rm_destroy(&rm);
  gpu_swapchain_destroy(&device, &swapchain);
  gpu_destroy(&device);

  glfwDestroyWindow(window);
  glfwTerminate();
  glslang_finalize_process();
  return 0;
}