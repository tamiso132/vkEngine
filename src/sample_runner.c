#include "command.h"
#include "common.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "resmanager.h"
#include "sample_interface.h"
#include "submit_manager.h"
#include "system_manager.h"
#include <GLFW/glfw3.h>
// --- Private Prototypes ---

void run_sample(Sample *sample, GLFWwindow *window) {
  // 1. Initiera samplet
  auto *device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *swapchain = SYSTEM_GET(SYSTEM_TYPE_SWAPCHAIN, M_Swapchain);
  auto *sm = SYSTEM_GET(SYSTEM_TYPE_SUBMIT, M_Submit);
  auto *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  auto *pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);

  CmdBuffer cmd = cmd_init(device->device, device->graphics_family);
  int width = 0, height = 0;
  SampleContext ctx = {
      .cmd = cmd,
      .gpu = device,
      .extent = swapchain->extent,
      .pm = pm,
      .pr = pr,
      .rm = rm,
  };

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
      swapchain_resize(device, rm, swapchain, &new_extent);

      // Låt samplet veta att vi har ändrat storlek (fixa depth/render targets)
      if (sample->on_resize) {
        sample->on_resize(sample, &ctx);
      }
      continue;
    }

    // Börja ramen
    m_system_update();
    sm_begin_frame(sm);
    sm_acquire_swapchain(sm, swapchain);

    ctx.swap_img = swapchain_get_image(swapchain);

    cmd_begin(device->device, cmd);
    cmd_bind_bindless(cmd, rm, swapchain->extent);

    // Transition: Swapchain -> Render Target
    ResHandle swap_img = swapchain_get_image(swapchain);
    // ImageBarrierInfo color_barrier = {.img_handle = swap_img,
    //                                   .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    //                                   .src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    //                                   .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                   .dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                   .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(rm, cmd.buffer, &color_barrier);

    cmd_sync_image(cmd, rm, swap_img, STATE_COLOR, ACCESS_READ);

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
    // rm_image_sync(rm, cmd.buffer, &present_barrier);
    cmd_sync_image(cmd, rm, swap_img, STATE_PRESENT, ACCESS_READ);
    cmd_end(device->device, cmd);

    // Submit & Present
    sm_work(sm, swapchain, cmd.buffer, true, true);
    sm_present(sm, swapchain);
  }

  vkDeviceWaitIdle(device->device);

  if (sample->destroy) {
    sample->destroy(sample);
  }

  // cmd_destroy(device->device, cmd); // Om du har en sådan funktion
}
// --- Private Functions ---
