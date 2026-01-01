#include "command.h"
#include "gpu/pipeline_hotreload.h"
#include "resmanager.h"
#include "sample_interface.h"
#include <GLFW/glfw3.h>

void run_sample(Sample *sample, Managers *mg, GPUDevice *device, GLFWwindow *window, GPUSwapchain *swapchain) {
  // 1. Initiera samplet

  CmdBuffer cmd = cmd_init(device->device, device->graphics_family);
  int width = 0, height = 0;
  SampleContext ctx = {.mg = mg, .device = device, .swapchain = swapchain, .extent = swapchain->extent, .cmd = cmd};

  if (sample->init) {
    sample->init(sample, &ctx);
  }

  // --- Main Loop ---
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    glfwGetFramebufferSize(window, &width, &height);
    // Hantera minimering
    while (width == 0 || height == 0) {
      glfwWaitEvents();
      glfwGetFramebufferSize(window, &width, &height);
    }

    // Hantera Resize
    if (swapchain->extent.height != height || swapchain->extent.width != width) {
      vkDeviceWaitIdle(device->device);

      VkExtent2D new_extent = {.width = width, .height = height};
      swapchain_resize(device, mg->rm, swapchain, &new_extent);

      // Låt samplet veta att vi har ändrat storlek (fixa depth/render targets)
      if (sample->on_resize) {
        sample->on_resize(sample, &ctx);
      }
      continue;
    }

    // Hot-reload system
    fm_poll(mg->fm);
    pr_update_modifed(mg->reloader);

    // Börja ramen
    submit_begin_frame(mg->sm);
    submit_acquire_swapchain(mg->sm, swapchain);

    cmd_begin(device->device, cmd);
    cmd_bind_bindless(cmd, mg->rm, swapchain->extent);

    // Transition: Swapchain -> Render Target
    ResHandle swap_img = swapchain_get_image(swapchain);
    // ImageBarrierInfo color_barrier = {.img_handle = swap_img,
    //                                   .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    //                                   .src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    //                                   .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                   .dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                   .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(mg->rm, cmd.buffer, &color_barrier);

    cmd_sync_image(cmd, mg->rm, swap_img, STATE_COLOR, ACCESS_READ);

    if (sample->render) {
      sample->render(sample, &ctx);
    }
    // ------------------------

    // Transition: Render Target -> Present
    // ImageBarrierInfo present_barrier = {.img_handle = swap_img,
    //                                     .src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                     .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    //                                     .src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                     .dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //                                     .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(mg->rm, cmd.buffer, &present_barrier);
    cmd_sync_image(cmd, mg->rm, swap_img, STATE_PRESENT, ACCESS_READ);
    cmd_end(device->device, cmd);

    // Submit & Present
    submit_work(mg->sm, swapchain, cmd.buffer, true, true);
    submit_present(mg->sm, swapchain);
  }

  vkDeviceWaitIdle(device->device);

  if (sample->destroy) {
    sample->destroy(sample, mg);
  }

  // cmd_destroy(device->device, cmd); // Om du har en sådan funktion
}
