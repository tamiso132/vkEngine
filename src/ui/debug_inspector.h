#pragma once
#include "resource/resmanager.h"
#include "shaders/rt/rt_shared.glsl" // Must include your shared constants (DBG_MAX_RECORDS, etc.)
#include <stdbool.h>
#include <stdint.h>

// --- State Struct ---
typedef struct DebugInspectorState {
  bool active;
  int x, y;
  uint32_t count;
  DbgRecord records[DBG_MAX_RECORDS];
} DebugInspectorState;

// --- API ---

// PUBLIC FUNCTIONS
void dbgr_fetch_to_state(M_GPU *gpu, M_Resource *rm, ResHandle buf_handle, uint32_t width, int x, int y,
                         DebugInspectorState *state);
void debug_ui_layout(DebugInspectorState *state);
// END PUBLIC FUNCTIONS
