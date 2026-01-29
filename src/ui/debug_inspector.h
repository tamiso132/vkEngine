// debug_inspector.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <clay.h>
#include "shaders/rt/rt_shared.glsl"


#ifndef DBG_INSPECTOR_ARENA_SIZE
#define DBG_INSPECTOR_ARENA_SIZE (32u * 1024u)
#endif

#ifndef DBG_INSPECTOR_DEFAULT_CAP
#define DBG_INSPECTOR_DEFAULT_CAP 256u
#endif

// ---------- Unified value container (typed, up to 4 lanes) ----------
typedef struct DebugValue {
  uint8_t type;     // DBG_T_*
  uint8_t lanes;    // 1..4
  union {
    uint32_t u32[4];
    int32_t  i32[4];
    float    f32[4];
  };
} DebugValue;

// ---------- Fixed item kinds (no custom callbacks) ----------
typedef enum DebugItemKind {
  DBG_ITEM_TEXT = 1,
  DBG_ITEM_KV_TEXT,
  DBG_ITEM_VALUE,        // label + DebugValue
  DBG_ITEM_SECTION_BEGIN,
  DBG_ITEM_SECTION_END,
  DBG_ITEM_PIXEL_PANEL,  // draws DebugInspectorPixelState
  DBG_ITEM_GPU_RECORD,   // event/key/type + formatted value from u32 words
} DebugItemKind;

typedef struct DebugItem {
  DebugItemKind kind;

  // TEXT / SECTION_BEGIN
  Clay_String text;

  // KV_TEXT / VALUE
  Clay_String label;
  Clay_String kv_value_text; // for KV_TEXT only

  // VALUE
  DebugValue value;

  // GPU_RECORD
  uint32_t header;
  uint32_t w0, w1, w2, w3;

  uint32_t stable_id; // optional CLAY_IDI seed (0 is fine)
} DebugItem;

typedef struct {
  bool active;
  int x, y;
  u32 count;
  DbgRecord records[DBG_MAX_RECORDS];
} DbgPinnedPixel;


// ---------- Main inspector ----------
typedef struct DebugInspector {
  bool open;

  // Per-frame list (cleared on begin_frame)
  DebugItem* items;
  uint32_t   item_count;
  uint32_t   item_cap;

  // Frame arena (strings must live through Clay render)
  char   arena[DBG_INSPECTOR_ARENA_SIZE];
  size_t arena_cursor;

  // Persistent pixel inspector state (updated by your CPU tick)
  DbgPinnedPixel pixel;

  // Styling (optional)
  Clay_Color panel_bg;
  Clay_Color row_bg;
  Clay_Color text_col;
  Clay_Color val_col;
} DebugInspector;

// ---------- Lifetime ----------
void debug_inspector_init(DebugInspector* di, uint32_t initial_cap);
void debug_inspector_shutdown(DebugInspector* di);

// Call once per frame BEFORE add_* (clears list + resets arena).
void debug_inspector_begin_frame(DebugInspector* di);

// ---------- Add methods (fixed set) ----------
void debug_inspector_section_begin(DebugInspector* di, const char* title);
void debug_inspector_section_end(DebugInspector* di);

void debug_inspector_add_text(DebugInspector* di, const char* fmt, ...);
void debug_inspector_add_kv_text(DebugInspector* di, const char* key, const char* fmt, ...);

// Typed value rows
void debug_inspector_add_u32(DebugInspector* di, const char* label, uint32_t v);
void debug_inspector_add_i32(DebugInspector* di, const char* label, int32_t v);
void debug_inspector_add_f32(DebugInspector* di, const char* label, float v);

void debug_inspector_add_vec2(DebugInspector* di, const char* label, float x, float y);
void debug_inspector_add_vec3(DebugInspector* di, const char* label, float x, float y, float z);
void debug_inspector_add_vec4(DebugInspector* di, const char* label, float x, float y, float z, float w);

void debug_inspector_add_ivec2(DebugInspector* di, const char* label, int32_t x, int32_t y);
void debug_inspector_add_ivec3(DebugInspector* di, const char* label, int32_t x, int32_t y, int32_t z);
void debug_inspector_add_uvec2(DebugInspector* di, const char* label, uint32_t x, uint32_t y);
void debug_inspector_add_uvec3(DebugInspector* di, const char* label, uint32_t x, uint32_t y, uint32_t z);

// GPU record (packed header + up to 4 u32 payload words)
void debug_inspector_add_gpu_record_u32words(DebugInspector* di, uint32_t header,
                                            uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3);

// Pixel inspector panel control
// - Call this from your CPU readback tick after you memcpy records.
void debug_inspector_set_pixel_state(DebugInspector* di, const DbgPinnedPixel* s);
// - Add an item that draws the pixel panel this frame.
void debug_inspector_add_pixel_panel(DebugInspector* di);

// ---------- Draw (pure Clay; iterates items once) ----------
void debug_inspector_draw(DebugInspector* di);
