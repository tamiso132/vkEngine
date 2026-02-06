// src/sample_runner.c
#include "resource/resmanager.h"
#include "sample_interface.h"

// --- Project headers (grouped) ---
#include "command.h"
#include "common.h"
#include "gpu/pipeline_hotreload.h"
#include "gpu/swapchain.h"
#include "input.h"
#include "readback.h"
#include "submit_manager.h"
#include "system_manager.h"
#include "transfer_queue.h"
#include "util.h"
#include "window.h"

#include "debug_ui/debug_inspector.h"
#include "debug_ui/nuklear_backend.h"
#include "gpu/swapchain.h"

// NOTE: Including a .c is usually a smell (ODR / build hygiene).
// Prefer `#include "raycam.h"` and compile raycam.c separately.
// Keeping your existing include here so this file remains drop-in.

// --- Types ---

typedef struct SampleRunner {
  M_GPU *gpu;
  M_Resource *rm;
  M_Submit *sm;

  TWindow main_win;
  TWindow debug_win;

  Input main_input;
  Input debug_input;

  NuklearBackend nuklear;
  CmdBuffer cmd_main;
  CmdBuffer cmd_dbg;
  SampleContext ctx;

  ReadBackBuffer read_back;

  bool is_paused;
  double last_time;
} SampleRunner;

// --- Private Phases ---

/** * Phase 1: Lifecycle & Windowing
 * Handles minimization, resizing, and closing logic.
 */
static bool _handle_lifecycle(SampleRunner *r, Sample *sample) {
  int w, h;
  glfwGetFramebufferSize(r->main_win.raw_window, &w, &h);

  // If minimized, block until restored
  while (w == 0 || h == 0) {
    glfwWaitEvents();
    glfwGetFramebufferSize(r->main_win.raw_window, &w, &h);
    if (glfwWindowShouldClose(r->main_win.raw_window))
      return false;
  }

  // Handle Resize
  if (r->main_win.swapchain.extent.width != (u32)w || r->main_win.swapchain.extent.height != (u32)h) {
    vkDeviceWaitIdle(r->gpu->device);
    // swapchain_resize(&r->main_win.swapchain, w, h);
    if (sample->on_resize)
      sample->on_resize(sample, &r->ctx);
  }

  return !glfwWindowShouldClose(r->main_win.raw_window);
}

/** * Phase 2: CPU Update
 * Updates input, cameras, and engine systems.
 */
static void _update_systems(SampleRunner *r, double dt) {
  input_update(&r->main_input);
  input_update(&r->debug_input);

  if (input_key_pressed(&r->main_input, GLFW_KEY_P)) {
    r->is_paused = !r->is_paused;
  }

  if (input_mouse_pressed(&r->debug_input, GLFW_MOUSE_BUTTON_LEFT)) {
    ivec2 mouse_pos = {};
    input_get_mouse_position(&r->debug_input, r->ctx.dbg_mouse_pos);
  }

  if (!r->is_paused) {
    camera_update(&r->ctx.cam, r->main_win.raw_window, dt);
    m_system_update();
    rm_on_new_frame(r->rm);
    transfer_on_new_frame(r->ctx.tq);
  }
}

static void _record_main_commands(SampleRunner *r, Sample *sample) {

  cmd_begin(r->gpu->device, r->cmd_main);
  // Submit Main Pass
  VkSemaphore main_done = r->main_win.swapchain.sem_render_finished[r->main_win.swapchain.current_img_idx];
  sm_add_signal(r->sm, main_done, 0, SM_STAGE_COLOR_ATTACHMENT_OUTPUT);
  sm_add_signal(r->sm, nuklear_backend_get_wait_binary(&r->nuklear), 0, SM_STAGE_ALL_COMMANDS);

  cmd_bind_bindless(r->cmd_main, r->rm, r->main_win.swapchain.extent);

  if (sample->render) {
    // Transition to color attachment, render, then transition to present
    cmd_sync_image(r->cmd_main, r->rm, r->ctx.swap_img, STATE_COLOR, ACCESS_READ);
    cmd_sync_buffer(r->cmd_main, r->rm, readback_get_handle(&r->read_back), STATE_SHADER, ACCESS_WRITE);
    sample->render(sample, &r->ctx);
  }

  cmd_sync_image(r->cmd_main, r->rm, r->ctx.swap_img, STATE_PRESENT, ACCESS_READ);
  // SET DEBUG SWAPCHAIN AS READ ONLY
  cmd_sync_image(r->cmd_main, r->rm, swapchain_get_image(&r->debug_win.swapchain), STATE_SHADER, ACCESS_READ);

  cmd_end(r->gpu->device, r->cmd_main);
  sm_submit(r->sm, r->cmd_main.buffer, false);
}

static void _record_debug_commands(SampleRunner *r) {

  cmd_begin(r->gpu->device, r->cmd_dbg);

  // Submit UI Pass
  VkSemaphore ui_done = nuklear_backend_render(&r->nuklear, &r->debug_win, r->gpu);

  // Final Frame Synchronization
  VkSemaphore debug_done = swapchain_get_render_done_semp(&r->debug_win.swapchain);
  sm_add_wait(r->sm, ui_done, 0, SM_STAGE_COMPUTE_SHADER);
  sm_add_signal(r->sm, debug_done, 0, SM_STAGE_COMPUTE_SHADER);

  cmd_sync_image(r->cmd_dbg, r->rm, swapchain_get_image(&r->debug_win.swapchain), STATE_PRESENT, ACCESS_READ);
  cmd_end(r->gpu->device, r->cmd_dbg);
  // Signals the timeline semaphore that the whole frame is finished
  sm_submit(r->sm, r->cmd_dbg.buffer, true);
}

static void _present_frame(SampleRunner *r) {
  transfer_submit_on_frame_end(r->ctx.tq);
  sm_present(r->sm, &r->debug_win.swapchain);
  sm_present(r->sm, &r->main_win.swapchain);
}

void run_sample(Sample *sample) {
  // Initialization (Simplified)
  SampleRunner r = {0};
  r.gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  r.rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  r.sm = sm_init(r.gpu->device, r.gpu->graphics_queue);

  window_init(&r.main_win, r.gpu, r.rm, 800, 600, "Main View", "MainSC");
  window_init(&r.debug_win, r.gpu, r.rm, 800, 600, "Debug View", "DebugSC");

  input_init(&r.main_input, r.main_win.raw_window);
  input_init(&r.debug_input, r.debug_win.raw_window);

  r.cmd_main = cmd_init(r.gpu->device, r.gpu->graphics_family);
  r.cmd_dbg = cmd_init(r.gpu->device, r.gpu->graphics_family);

  r.nuklear = nuklear_backend_init(r.gpu, r.rm, &r.debug_win);

  r.ctx = (SampleContext){
      .cmd = r.cmd_main,
      .gpu = r.gpu,
      .rm = r.rm,
      .extent = r.main_win.swapchain.extent,
      .cam = camera_init(r.main_win.swapchain.extent, 70),
      .tq = transfer_init(r.gpu->device, r.gpu->transfer_queue, r.gpu->transfer_family, r.gpu->allocator, MIB(250), 1),

  };

  r.ctx.pr = SYSTEM_GET(SYSTEM_TYPE_HOTRELOAD, M_HotReload);
  r.ctx.gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  r.ctx.rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  r.ctx.pm = SYSTEM_GET(SYSTEM_TYPE_PIPELINE, M_Pipeline);

  cmd_begin(r.gpu->device, r.cmd_main);

  r.last_time = glfwGetTime();
  editor_pixel_editor editor = {};
  editor_pixel_meta_main_init(&editor, r.debug_win.width, r.debug_win.height, NULL);
  ReadBackBuffer readback = {};
  readback_init(&r.read_back, r.rm, r.debug_win.swapchain.extent);

  // Initial Sample Load
  if (sample->init) {
    transfer_on_new_frame(r.ctx.tq);
    sample->init(sample, &r.ctx);
  }

  cmd_end(r.gpu->device, r.cmd_main);
  sm_submit(r.sm, r.cmd_main.buffer, true);
  vkDeviceWaitIdle(r.gpu->device);

  while (_handle_lifecycle(&r, sample)) {
    glfwPollEvents();

    double time_now = glfwGetTime();
    double dt = time_now - r.last_time;
    r.last_time = time_now;

    if (r.is_paused)
      continue;

    // Start GPU Frame
    sm_begin_frame(r.sm);
    _update_systems(&r, dt);

    // TODO: move later
    readback_write_to_editor(&r.read_back, &editor);

    r.ctx.readback_idx = readback_get_push_id(&readback, r.rm);

    // Acquire both windows
    sm_acquire_swapchain(r.sm, &r.main_win.swapchain, SM_STAGE_COMPUTE_SHADER);
    sm_acquire_swapchain(r.sm, &r.debug_win.swapchain, SM_STAGE_COMPUTE_SHADER);

    r.ctx.swap_img = swapchain_get_image(&r.main_win.swapchain);

    // DRAW DEBUG UI
    nuklear_backend_new_frame(&r.nuklear);
    struct nk_context *draw_ctx = nuklear_backend_get_draw_ctx(&r.nuklear);
    editor_pixel_meta_main_draw(&editor, draw_ctx);

    _record_main_commands(&r, sample);

    // Render DEBUG Window
    _record_debug_commands(&r);

    // Finish
    _present_frame(&r);
  }

  // --- Cleanup ---
  vkDeviceWaitIdle(r.gpu->device);
  if (sample->destroy)
    sample->destroy(sample);
  sm_destroy(r.sm);
}
