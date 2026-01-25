#include <clay.h>

#include "command.h"
#include "common.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "input.h"
#include "raycam.c"
#include "readback.h"
#include "sample_interface.h"
#include "submit_manager.h"
#include "system_manager.h"
#include "transfer_queue.h"
#include <GLFW/glfw3.h>

#include "ui/clay_backend.h"
#include "ui/debug_inspector.h"

#include "window.h"
// --- Private Prototypes ---
// Add this helper at the top of src/sample_runner.c

void run_sample(Sample *sample) {
  // 1. Initiera samplet
  auto *device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *sm = SYSTEM_GET(SYSTEM_TYPE_SUBMIT, M_Submit);
  auto *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  auto *pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);

  TWindow main_win = {0};
  window_init(&main_win, device, rm, 800, 600, "Main View", "MainSC");

  TWindow debug_win = {0};
  window_init(&debug_win, device, rm, 800, 600, "Debug View (Click me)", "DebugSC");

  Input input = {0};
  input_init(&input, main_win.raw_window);

  Input debug_input = {0};
  input_init(&debug_input, debug_win.raw_window);

  CmdBuffer cmd = cmd_init(device->device, device->graphics_family);
  int width = 0, height = 0;

  SampleContext ctx = {
      .cmd = cmd,
      .gpu = device,
      .extent = main_win.swapchain.extent,
      .pm = pm,
      .pr = pr,
      .rm = rm,
      .cam = camera_init(main_win.swapchain.extent, 70),
      .tq = transfer_init(device->device, device->transfer_queue, device->transfer_family, device->allocator, MIB(250),
                          1),
  };
  cmd_begin(device->device, cmd);
  transfer_on_new_frame(ctx.tq);
  if (sample->init) {

    sample->init(sample, &ctx);
  }

  // CLAAY STUFF
  ClayContext clay_ctx;
  clay_backend_init(&clay_ctx, device, rm, pr, ctx.cmd, main_win.swapchain.format, 800, 600);

  cmd_end(device->device, cmd);
  sm_work(sm, &main_win.swapchain, cmd.buffer, false, false);
  transfer_submit_on_frame_end(ctx.tq);
  vkDeviceWaitIdle(device->device);

  double last_time = glfwGetTime();
  bool is_paused = false;

  ResHandle dbg_buf = dbgr_create_buffer(rm, 800, 600);
  uint32_t dbg_id = rm_get_buffer_index(rm, dbg_buf);

  DebugInspectorState inspector = {0};
  // Initialization is now one line (plus cmd management)
  // --- Main Loop ---
  while (!glfwWindowShouldClose(main_win.raw_window)) {
    glfwPollEvents();
    double time_now = glfwGetTime();
    double dt = time_now - last_time;
    last_time = time_now;

    input_update(&input);
    input_update(&debug_input);

    bool p_is_pressed = (glfwGetKey(main_win.raw_window, GLFW_KEY_P) == GLFW_PRESS);
    if (input_key_pressed(&input, GLFW_KEY_P)) {
      is_paused = !is_paused;
      LOG_INFO("[Runner] Simulation %s", is_paused ? "PAUSED" : "RUNNING");
      continue;
    }

    if (input_button_pressed(&debug_input, GLFW_MOUSE_BUTTON_LEFT)) {
      double x, y;
      glfwGetCursorPos(debug_win.raw_window, &x, &y);
      // This blocks the CPU/GPU, so only run ONCE per click
      dbgr_analyze_pixel(device, rm, dbg_buf, 800, (int)x, (int)y);
    }

    glfwGetFramebufferSize(main_win.raw_window, &width, &height);
    // Hantera minimering
    while (width == 0 || height == 0) {
      glfwWaitEvents();
      glfwGetFramebufferSize(main_win.raw_window, &width, &height);
    }

    // Hantera Resize
    if (main_win.swapchain.extent.height != height || main_win.swapchain.extent.width != width) {
      vkDeviceWaitIdle(device->device);

      VkExtent2D new_extent = {.width = width, .height = height};
      swapchain_resize(device, rm, &main_win.swapchain, &new_extent);
      // RESIZE DEBUG TOO

      // Låt samplet veta att vi har ändrat storlek (fixa depth/render targets)
      if (sample->on_resize) {
        sample->on_resize(sample, &ctx);
      }
      continue;
    }

    // Börja ramen
    camera_update(&ctx.cam, main_win.raw_window, dt);
    m_system_update();
    sm_begin_frame(sm);
    transfer_on_new_frame(ctx.tq);

    rm_on_new_frame(rm);

    sm_acquire_swapchain(sm, &main_win.swapchain);

    ctx.swap_img = swapchain_get_image(&main_win.swapchain);

    cmd_begin(device->device, cmd);
    cmd_bind_bindless(cmd, rm, main_win.swapchain.extent);

    Clay_SetLayoutDimensions((Clay_Dimensions){(float)width, (float)height});
    Clay_BeginLayout();
    debug_ui_layout(&inspector);
    Clay_RenderCommandArray ui_cmds = Clay_EndLayout();

    // ... Render ...
    if (inspector.active) {
      clay_backend_render(&clay_ctx, &cmd, &ui_cmds, width, height);
    }
    // Transition: Swapchain -> Render Target
    // ImageBarrierInfo color_barrier = {.img_handle = swap_img,
    //                                   .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    //                                   .src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    //                                   .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                   .dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                   .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(rm, cmd.buffer, &color_barrier);

    cmd_sync_image(cmd, rm, ctx.swap_img, STATE_COLOR, ACCESS_READ);

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
    cmd_sync_image(cmd, rm, ctx.swap_img, STATE_PRESENT, ACCESS_READ);
    cmd_end(device->device, cmd);

    // Submit & Present
    transfer_submit_on_frame_end(ctx.tq);
    sm_work(sm, &main_win.swapchain, cmd.buffer, true, true);
    sm_present(sm, &main_win.swapchain);
  }

  vkDeviceWaitIdle(device->device);

  if (sample->destroy) {
    sample->destroy(sample);
  }

  // cmd_destroy(device->device, cmd); // Om du har en sådan funktion
}
// --- Private Functions ---
