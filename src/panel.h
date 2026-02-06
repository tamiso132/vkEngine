#pragma once
#include <volk.h>

#include "util.h"

typedef struct WindowManager WindowManager;

typedef enum WindowPanelType {
  WIN_PANEL_TYPE_MIDDLE,
  WIN_PANEL_TYPE_BOTTOM,
  WIN_PANEL_TYPE_RIGHT,
  WIN_PANEL_TYPE_LEFT,
  WIN_PANEL_TYPE__COUNT,
} WindowPanelType;

typedef enum WinStateFlags {
  WIN_VISIBLE = 1u << 0,
  WIN_HOVERED = 1u << 1,
  WIN_FOCUSED = 1u << 2,
  WIN_CAPTURED = 1u << 3,
} WinStateFlags;

typedef enum WinEventFlags {
  WIN_EVENT_MOUSE_ENTER = 1u << 0,
  WIN_EVENT_MOUSE_LEAVE = 1u << 1,
  WIN_EVENT_FOCUS_GAIN = 1u << 2,
  WIN_EVENT_FOCUS_LOSS = 1u << 3,
  WIN_EVENT_RESIZED = 1u << 4,
  WIN_EVENT_BEGIN_CAPTURE = 1u << 5,
  WIN_EVENT_END_CAPTURE = 1u << 6,
} WinEventFlags;

typedef struct WindowRect {
  VkOffset2D offset;
  VkExtent2D size;
} WindowRect;

typedef struct WindowInstance {
  WindowRect rect;
  BitFlags32 win_event_flags;
  BitFlags32 win_state_flags;
} WindowInstance;

// PUBLIC FUNCTIONS
WindowInstance *window_manager_get_virtual(WindowManager *ctx, WindowPanelType panel_type);
WindowManager *window_manager_init(VkExtent2D window_extent, VkExtent2D middle_window);
void window_manager_tick(WindowManager *ctx);
// END PUBLIC FUNCTIONS
