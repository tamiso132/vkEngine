
#include "common.h"
#include "input.h"
#include "util.h"

#include "panel.h"

  const char *TAG = "WindowManager";

typedef struct WindowCallbacks {
  void (*on_focus)(WindowInstance *w);
  void (*on_blur)(WindowInstance *w);

  void (*on_mouse_enter)(WindowInstance *w, void *ctx);
  void (*on_mouse_leave)(WindowInstance *w, void *ctx);

  void (*on_resize)(WindowInstance *w, VkExtent2D new_size);
} WindowCallbacks;

typedef struct WindowManager {
  WindowInstance editor_panels[WIN_PANEL_TYPE__COUNT];
  Input input;

  WindowPanelType mouse_in;
  WindowPanelType focused_panel;
} WindowManager;

// --- Private Prototypes ---
static bool _boundary_check(ivec2 pos, WindowRect w);

static void _set_new_hover(WindowManager *ctx);

static void _clear_all_window_events(WindowManager *ctx);
static void _clear_current_hover(WindowManager *ctx);

WindowManager *window_manager_init(VkExtent2D window_extent, VkExtent2D middle_window) {

  WindowManager *ctx = calloc(sizeof(WindowManager), 1);

  WindowRect virt_middle_window = {};
  virt_middle_window.offset.x = (window_extent.width / 2) - middle_window.width / 2;
  virt_middle_window.offset.y = (window_extent.height / 2) - middle_window.height / 2;
  virt_middle_window.size = middle_window;

  WindowRect virt_left_window = {};
  virt_left_window.size.height = virt_middle_window.size.height;
  virt_left_window.size.width = virt_middle_window.offset.x;

  WindowRect virt_right_window = {};
  virt_right_window.size.height = virt_middle_window.size.height;
  virt_right_window.size.width = window_extent.width - virt_middle_window.offset.x - virt_middle_window.size.width;
  virt_right_window.offset.x = virt_middle_window.offset.x + virt_middle_window.size.width;

  WindowRect virt_bot_window = {};
  virt_bot_window.offset.y = virt_right_window.size.height;
  virt_bot_window.size.width = window_extent.width;
  virt_bot_window.size.height = window_extent.height - middle_window.height;

  ctx->editor_panels[WIN_PANEL_TYPE_MIDDLE].rect = virt_middle_window;
  ctx->editor_panels[WIN_PANEL_TYPE_LEFT].rect = virt_left_window;
  ctx->editor_panels[WIN_PANEL_TYPE_RIGHT].rect = virt_right_window;
  ctx->editor_panels[WIN_PANEL_TYPE_BOTTOM].rect = virt_bot_window;

  _set_new_hover(ctx);

LOG_INFO_TAG(TAG, "virt_middle_window: off=(%d,%d) size=(%u,%u)",
             virt_middle_window.offset.x, virt_middle_window.offset.y,
             virt_middle_window.size.width, virt_middle_window.size.height);

LOG_INFO_TAG(TAG, "virt_left_window: off=(%d,%d) size=(%u,%u)",
             virt_left_window.offset.x, virt_left_window.offset.y,
             virt_left_window.size.width, virt_left_window.size.height);

LOG_INFO_TAG(TAG, "virt_right_window: off=(%d,%d) size=(%u,%u)",
             virt_right_window.offset.x, virt_right_window.offset.y,
             virt_right_window.size.width, virt_right_window.size.height);

LOG_INFO_TAG(TAG, "virt_bot_window: off=(%d,%d) size=(%u,%u)",
             virt_bot_window.offset.x, virt_bot_window.offset.y,
             virt_bot_window.size.width, virt_bot_window.size.height);

  return ctx;
}

WindowInstance *window_manager_get_virtual(WindowManager *ctx, WindowPanelType panel_type) {
  return &ctx->editor_panels[panel_type];
}

void window_manager_tick(WindowManager *ctx) {
  ivec2 mouse_pos;
  input_get_mouse_position(&ctx->input, mouse_pos);

  _clear_all_window_events(ctx);

  bool is_inside_same_panel = _boundary_check(mouse_pos, ctx->editor_panels[ctx->mouse_in].rect);

  if (!is_inside_same_panel) {
    _clear_current_hover(ctx);
    _set_new_hover(ctx);
  }
}

// --- Private Functions ---

static bool _boundary_check(ivec2 pos, WindowRect w) {
  int x0 = w.offset.x;
  int y0 = w.offset.y;

  int x1 = x0 + (int)w.size.width;
  int y1 = y0 + (int)w.size.height;

  bool inside_x = (pos[0] >= x0) && (pos[0] < x1);
  bool inside_y = (pos[1] >= y0) && (pos[1] < y1);

  return inside_x && inside_y;
}

static void _set_new_hover(WindowManager *ctx) {
  ivec2 mouse_pos;
  input_get_mouse_position(&ctx->input, mouse_pos);

  for (u32 i = 0; i < WIN_PANEL_TYPE__COUNT; i++) {
    bool is_inside = _boundary_check(mouse_pos, ctx->editor_panels[i].rect);

    if (is_inside) {
      bitflag_set(&ctx->editor_panels[i].win_event_flags, WIN_EVENT_MOUSE_ENTER);
      bitflag_set(&ctx->editor_panels[i].win_state_flags, WIN_HOVERED);
      ctx->mouse_in = i;
      return;
    }
  }
}

static void _clear_all_window_events(WindowManager *ctx) {
  for (u32 i = 0; i < WIN_PANEL_TYPE__COUNT; i++) {
    ctx->editor_panels[i].win_event_flags.bits = 0;
  }
}

static void _clear_current_hover(WindowManager *ctx) {
  bitflag_set(&ctx->editor_panels[ctx->mouse_in].win_event_flags, WIN_EVENT_MOUSE_LEAVE);
  bitflag_clear(&ctx->editor_panels[ctx->mouse_in].win_state_flags, WIN_HOVERED);
}
