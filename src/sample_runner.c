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
#include <vulkan/vulkan_core.h>

#include "ui/clay_backend.h"
#include "ui/debug_inspector.h"

#include "util.h"
#include "window.h"
// Add this helper at the top of src/sample_runner.c

// --- Private Prototypes ---

static void debug_ui_frame(DebugInspector* di, float dt);

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

  // CLAAY STUFF
  ClayContext clay_ctx;
  clay_backend_init(&clay_ctx, device, rm, pr, ctx.cmd, main_win.swapchain.format, 800, 600);

  cmd_end(device->device, cmd_main);
  sm_work(sm, &main_win.swapchain, 1, cmd_main.buffer, false, false);
  transfer_submit_on_frame_end(ctx.tq);
  vkDeviceWaitIdle(device->device);

  double last_time = glfwGetTime();
  bool is_paused = false;

  DebugInspector di = {0};
  debug_inspector_init(&di, 0);      // 0 = default cap
  ReadBackBuffer readback = {};
  readback_init(&readback, ctx.rm,main_win.swapchain.extent);
  // Initialization is now one line (plus cmd management)
  // --- Main Loop ---
  while (!glfwWindowShouldClose(main_win.raw_window)) {
    glfwPollEvents();
    LOG_INFO("New Frame");
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
  
    debug_ui_frame(&di, 1.0);


    clay_backend_render(&clay_ctx, cmd_main, ctx.rm,&debug_win.swapchain, width, height);
    // Transition: Swapchain -> Render Target
    // ImageBarrierInfo color_barrier = {.img_handle = swap_img,
    //                                   .src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    //                                   .src_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    //                                   .dst_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
    //                                   .dst_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                   .dst_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    // rm_image_sync(rm, cmd.buffer, &color_barrier);

    cmd_sync_image(cmd_main, rm, ctx.swap_img, STATE_COLOR, ACCESS_READ);

    if (sample->render) {
     
      ResHandle main_img = swapchain_get_image(&main_win.swapchain);
      
      RenderingBeginInfo main_begin = {.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .h = main_win.swapchain.extent.height, 
      .w = main_win.swapchain.extent.width, .colors = &main_img, .colors_count = 1};

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
    cmd_sync_image(cmd_main, rm, ctx.swap_img, STATE_PRESENT, ACCESS_READ);
    cmd_end(device->device, cmd_main);

    // Submit & Present
    transfer_submit_on_frame_end(ctx.tq);
    M_Swapchain swapchains[] ={main_win.swapchain, debug_win.swapchain}; 
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
  union { float f; u32 u; } c;
  c.f = f;
  return c.u;
}

static void debug_inspector_test_fill_pixel(DebugInspector* di) {
  // Make sure the panel shows something even without GPU readback
  di->pixel.active = true;
  di->pixel.x = 123;
  di->pixel.y = 456;
  di->pixel.count = 3;

  // header packing: event<<24 | key<<16 | type<<8 | part
  di->pixel.records[0].header = (7u<<24) | (2u<<16) | (DBG_T_F32<<8) | 0u;
  di->pixel.records[0].y = f2u(1.0f);
  di->pixel.records[0].z = 0;
  di->pixel.records[0].w = 0;

  di->pixel.records[1].header = (7u<<24) | (3u<<16) | (DBG_T_VEC3<<8) | 0u;
  di->pixel.records[1].y = f2u(1.0f);
  di->pixel.records[1].z = f2u(2.0f);
  di->pixel.records[1].w = f2u(3.0f);

  di->pixel.records[2].header = (8u<<24) | (1u<<16) | (DBG_T_U32<<8) | 0u;
  di->pixel.records[2].y = 1337u;
  di->pixel.records[2].z = 0;
  di->pixel.records[2].w = 0;
}

static void debug_ui_frame(DebugInspector* di, float dt) {
  // IMPORTANT: Clay layout must begin before you emit CLAY nodes


  debug_inspector_begin_frame(di);

  debug_inspector_section_begin(di, "Frame");
  debug_inspector_add_kv_text(di, "FrameIdx", "%u", 1u);
  debug_inspector_add_f32(di, "dt", dt);
  debug_inspector_section_end(di);

  // Force some pixel data so the panel isn't empty
  debug_inspector_test_fill_pixel(di);

  debug_inspector_add_pixel_panel(di);
  debug_inspector_draw(di);

  // DO NOT call Clay_EndLayout() here if clay_backend_render() already does it.
  // Your clay_backend_render() currently calls Clay_EndLayout() and renders it.
}
