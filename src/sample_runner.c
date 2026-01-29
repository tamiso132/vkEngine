
#include "command.h"
#include "common.h"
#include "debug_ui/debug_inspector.h"
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

#include "util.h"
#include "window.h"

#include "debug_ui/debug_inspector.h"
#include "debug_ui/nuklear_backend.h"
// Add this helper at the top of src/sample_runner.c

// --- Private Prototypes ---

void run_sample(Sample *sample) {
  // 1. Initiera samplet
  auto *device = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  auto *pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);
  auto *pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);

  // WINDOW CREATION
  TWindow main_win = {0};
  window_init(&main_win, device, rm, 800, 600, "Main View", "MainSC");

  TWindow debug_win = {0};
  window_init(&debug_win, device, rm, 800, 600, "Debug View (Click me)", "DebugSC");

  // WINDOW INPUT CREATION, TODO, put it inside of TWindow
  Input input = {0};
  input_init(&input, main_win.raw_window);

  Input debug_input = {0};
  input_init(&debug_input, debug_win.raw_window);

  CmdBuffer cmd_main = cmd_init(device->device, device->graphics_family);
  CmdBuffer cmd_dbg = cmd_init(device->device, device->graphics_family);

  auto *sm = sm_init(device->device, device->graphics_queue);

  int width = 0, height = 0;

  SampleContext ctx = {
      .cmd = cmd_main,
      .gpu = device,
      .extent = main_win.swapchain.extent,
      .pm = pm,
      .pr = pr,
      .rm = rm,
      .cam = camera_init(main_win.swapchain.extent, 70),
      .tq = transfer_init(device->device, device->transfer_queue, device->transfer_family, device->allocator, MIB(250),
                          1),
  };
  cmd_begin(device->device, cmd_main);
  transfer_on_new_frame(ctx.tq);
  if (sample->init) {

    sample->init(sample, &ctx);
  }

  NuklearBackend nuklear_ctx = nuklear_backend_init(ctx.gpu, ctx.rm, &debug_win);

  cmd_end(device->device, cmd_main);
  editor_pixel_editor ed = {};
  editor_pixel_meta_main_init(&ed, debug_win.width, debug_win.height, 0);

  sm_work(sm, &main_win.swapchain, 1, cmd_main.buffer, false, false);
  transfer_submit_on_frame_end(ctx.tq);
  vkDeviceWaitIdle(device->device);

  double last_time = glfwGetTime();
  bool is_paused = false;

  ReadBackBuffer readback = {};
  readback_init(&readback, ctx.rm, main_win.swapchain.extent);
  // Initialization is now one line (plus cmd management)
  // --- Main Loop ---
  while (!glfwWindowShouldClose(main_win.raw_window)) {
    glfwPollEvents();
    double time_now = glfwGetTime();
    double dt = time_now - last_time;
    last_time = time_now;

    input_update(&input);
    input_update(&debug_input);

    glfwGetFramebufferSize(main_win.raw_window, &width, &height);

    // MINIMIZED WINDOW, MAIN WINDOW
    while (width == 0 || height == 0) {
      glfwWaitEvents();
      glfwGetFramebufferSize(main_win.raw_window, &width, &height);
    }

    // RESIZE HANDLING
    if (main_win.swapchain.extent.height != height || main_win.swapchain.extent.width != width) {
      vkDeviceWaitIdle(device->device);

      VkExtent2D new_extent = {.width = width, .height = height};
      // swapchain_resize(device, rm, &main_win.swapchain, &new_extent);
      //  RESIZE DEBUG TOO

      // Låt samplet veta att vi har ändrat storlek (fixa depth/render targets)
      if (sample->on_resize) {
        sample->on_resize(sample, &ctx);
      }
      continue;
    }

    sm_begin_frame(sm);

    if (input_key_pressed(&input, GLFW_KEY_P)) {
      is_paused = !is_paused;
      LOG_INFO("[Runner] Simulation %s", is_paused ? "PAUSED" : "RUNNING");
      continue;
    }

    // CLICKING A PIXEL IN DEBUG WINDOW
    if (input_button_pressed(&debug_input, GLFW_MOUSE_BUTTON_LEFT)) {
      double x, y;
      glfwGetCursorPos(debug_win.raw_window, &x, &y);
      // This blocks the CPU/GPU, so only run ONCE per click
    }

    // START GPU FRAME
    camera_update(&ctx.cam, main_win.raw_window, dt);
    m_system_update();
    rm_on_new_frame(rm);
    transfer_on_new_frame(ctx.tq);

    // AQUIRE SWAPCHAIN IMAGE
    sm_acquire_swapchain(sm, &main_win.swapchain);
    sm_acquire_swapchain(sm, &debug_win.swapchain);

    ctx.swap_img = swapchain_get_image(&main_win.swapchain);

    cmd_begin(device->device, cmd_main);
    cmd_bind_bindless(cmd_main, rm, main_win.swapchain.extent);

    nuklear_backend_new_frame(&nuklear_ctx);
    editor_pixel_meta_main_append_test_data(&ed);

    cmd_sync_image(cmd_main, rm, ctx.swap_img, STATE_COLOR, ACCESS_READ);

    editor_pixel_meta_main_draw(&ed, nuklear_ctx.ctx);
    if (sample->render) {

      ResHandle main_img = swapchain_get_image(&main_win.swapchain);

      RenderingBeginInfo main_begin = {.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                       .h = main_win.swapchain.extent.height,
                                       .w = main_win.swapchain.extent.width,
                                       .colors = &main_img,
                                       .colors_count = 1};

      sample->render(sample, &ctx);
    }

    nuklear_backend_render(&nuklear_ctx, cmd_main, ctx.gpu, &debug_win);

    // ------------------------

    // Transition: Render Target -> Present
    // ImageBarrierInfo present_barrier = {.img_handle = swap_img,
    //                                     .src_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                     .src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    //                                     .src_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                     .dst_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //                                     .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(rm, cmd.buffer, &present_barrier);
    cmd_sync_image(cmd_main, rm, ctx.swap_img, STATE_PRESENT, ACCESS_READ);
    cmd_end(device->device, cmd_main);

    // Submit & Present
    transfer_submit_on_frame_end(ctx.tq);
    M_Swapchain swapchains[] = {main_win.swapchain, debug_win.swapchain};
    sm_work(sm, swapchains, 2, cmd_main.buffer, true, true);

    sm_present(sm, &debug_win.swapchain);
    sm_present(sm, &main_win.swapchain);

    sm_on_frame_end(sm);
  }

  vkDeviceWaitIdle(device->device);

  if (sample->destroy) {
    sample->destroy(sample);
  }

  // cmd_destroy(device->device, cmd); // Om du har en sådan funktion
}
static inline u32 f2u(float f) {
  union {
    float f;
    u32 u;
  } c;
  c.f = f;
  return c.u;
}
