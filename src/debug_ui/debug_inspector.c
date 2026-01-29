// debug_inspector.c
#include "debug_inspector.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------
// Helpers
// ---------------------

static float u2f(uint32_t u) {
  union { uint32_t u; float f; } c;
  c.u = u;
  return c.f;
}

static void di_grow_items(DebugInspector* di, uint32_t min_cap) {
  uint32_t new_cap = di->item_cap ? di->item_cap : DBG_INSPECTOR_DEFAULT_CAP;
  while (new_cap < min_cap) new_cap *= 2;

  DebugItem* n = (DebugItem*)realloc(di->items, sizeof(DebugItem) * (size_t)new_cap);
  if (!n) return; // out of memory: just keep old buffer, silently drop pushes
  di->items = n;
  di->item_cap = new_cap;
}

static void di_push(DebugInspector* di, DebugItem it) {
  if (di->item_count + 1 > di->item_cap) {
    di_grow_items(di, di->item_count + 1);
  }
  if (di->item_count + 1 > di->item_cap) return; // still no space
  di->items[di->item_count++] = it;
}

static Clay_String di_fmt(DebugInspector* di, const char* fmt, va_list args) {
  if (!fmt) return (Clay_String){ .length = 0, .chars = "" };
  if (di->arena_cursor >= sizeof(di->arena)) return (Clay_String){ .length = 0, .chars = "" };

  char* ptr = &di->arena[di->arena_cursor];
  size_t remaining = sizeof(di->arena) - di->arena_cursor;

  va_list copy;
  va_copy(copy, args);
  int len = vsnprintf(ptr, remaining, fmt, copy);
  va_end(copy);

  if (len < 0) return (Clay_String){ .length = 0, .chars = "" };

  if ((size_t)len >= remaining) {
    len = (int)remaining - 1;
    ptr[len] = '\0';
  }

  di->arena_cursor += (size_t)len + 1;
  return (Clay_String){ .length = (uint32_t)len, .chars = ptr };
}

static Clay_String di_fmt1(DebugInspector* di, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Clay_String s = di_fmt(di, fmt, args);
  va_end(args);
  return s;
}

static Clay_String di_strdup(DebugInspector* di, const char* s) {
  if (!s) return (Clay_String){ .length = 0, .chars = "" };
  size_t len = strlen(s);
  size_t need = len + 1;
  if (di->arena_cursor + need > sizeof(di->arena)) return (Clay_String){ .length = 0, .chars = "" };

  char* dst = &di->arena[di->arena_cursor];
  memcpy(dst, s, len);
  dst[len] = '\0';
  di->arena_cursor += need;
  return (Clay_String){ .length = (uint32_t)len, .chars = dst };
}

static uint8_t dbg_type_lanes(uint32_t type) {
  switch (type) {
    case DBG_T_U32:
    case DBG_T_I32:
    case DBG_T_F32:   return 1;
    case DBG_T_VEC2:
    case DBG_T_IVEC2:
    case DBG_T_UVEC2: return 2;
    case DBG_T_VEC3:
    case DBG_T_IVEC3:
    case DBG_T_UVEC3: return 3;
    case DBG_T_VEC4:  return 4;
    default:          return 1;
  }
}

static Clay_String format_value(DebugInspector* di, const DebugValue* v) {
  const uint8_t t = v->type;
  const uint8_t n = v->lanes ? v->lanes : 1;

  // Scalars: a bit more precision; vectors: shorter
  switch (t) {
    case DBG_T_U32: return di_fmt1(di, "%u", v->u32[0]);
    case DBG_T_I32: return di_fmt1(di, "%d", v->i32[0]);
    case DBG_T_F32: return di_fmt1(di, "%.4f", v->f32[0]);

    case DBG_T_VEC2: return di_fmt1(di, "(%.3f, %.3f)", v->f32[0], v->f32[1]);
    case DBG_T_VEC3: return di_fmt1(di, "(%.3f, %.3f, %.3f)", v->f32[0], v->f32[1], v->f32[2]);
    case DBG_T_VEC4: return di_fmt1(di, "(%.3f, %.3f, %.3f, %.3f)", v->f32[0], v->f32[1], v->f32[2], v->f32[3]);

    case DBG_T_IVEC2: return di_fmt1(di, "(%d, %d)", v->i32[0], v->i32[1]);
    case DBG_T_IVEC3: return di_fmt1(di, "(%d, %d, %d)", v->i32[0], v->i32[1], v->i32[2]);

    case DBG_T_UVEC2: return di_fmt1(di, "(%u, %u)", v->u32[0], v->u32[1]);
    case DBG_T_UVEC3: return di_fmt1(di, "(%u, %u, %u)", v->u32[0], v->u32[1], v->u32[2]);

    default:
      // fallback: dump first lanes as u32
      if (n == 1) return di_fmt1(di, "0x%08X", v->u32[0]);
      if (n == 2) return di_fmt1(di, "(0x%08X, 0x%08X)", v->u32[0], v->u32[1]);
      if (n == 3) return di_fmt1(di, "(0x%08X, 0x%08X, 0x%08X)", v->u32[0], v->u32[1], v->u32[2]);
      return di_fmt1(di, "(0x%08X, 0x%08X, 0x%08X, 0x%08X)", v->u32[0], v->u32[1], v->u32[2], v->u32[3]);
  }
}

// Decode GPU u32 words into a DebugValue according to header type.
// Convention: w0..w3 are raw u32 bits; float types are bitcast.
static DebugValue value_from_gpu_words(uint32_t header, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
  DebugValue v = {0};
  uint32_t type = DBG_GET_TYPE(header);
  v.type = (uint8_t)type;
  v.lanes = dbg_type_lanes(type);

  uint32_t w[4] = { w0, w1, w2, w3 };

  switch (type) {
    case DBG_T_U32:
    case DBG_T_UVEC2:
    case DBG_T_UVEC3:
      v.u32[0] = w[0]; v.u32[1] = w[1]; v.u32[2] = w[2]; v.u32[3] = w[3];
      break;

    case DBG_T_I32:
    case DBG_T_IVEC2:
    case DBG_T_IVEC3:
      v.i32[0] = (int32_t)w[0]; v.i32[1] = (int32_t)w[1]; v.i32[2] = (int32_t)w[2]; v.i32[3] = (int32_t)w[3];
      break;

    case DBG_T_F32:
    case DBG_T_VEC2:
    case DBG_T_VEC3:
    case DBG_T_VEC4:
      v.f32[0] = u2f(w[0]); v.f32[1] = u2f(w[1]); v.f32[2] = u2f(w[2]); v.f32[3] = u2f(w[3]);
      break;

    default:
      v.u32[0] = w[0]; v.u32[1] = w[1]; v.u32[2] = w[2]; v.u32[3] = w[3];
      break;
  }

  return v;
}

// ---------------------
// Public API
// ---------------------

void debug_inspector_init(DebugInspector* di, uint32_t initial_cap) {
  memset(di, 0, sizeof(*di));
  di->open = true;

  di->panel_bg = (Clay_Color){ 40, 40, 40, 240 };
  di->row_bg   = (Clay_Color){ 60, 60, 60, 255 };
  di->text_col = (Clay_Color){ 255, 255, 255, 255 };
  di->val_col  = (Clay_Color){ 200, 200, 200, 255 };

  di->item_cap = initial_cap ? initial_cap : DBG_INSPECTOR_DEFAULT_CAP;
  di->items = (DebugItem*)malloc(sizeof(DebugItem) * (size_t)di->item_cap);
  if (!di->items) {
    di->item_cap = 0;
  }
}

void debug_inspector_shutdown(DebugInspector* di) {
  free(di->items);
  memset(di, 0, sizeof(*di));
}

void debug_inspector_begin_frame(DebugInspector* di) {
  di->item_count = 0;
  di->arena_cursor = 0;
}

void debug_inspector_section_begin(DebugInspector* di, const char* title) {
  DebugItem it = {0};
  it.kind = DBG_ITEM_SECTION_BEGIN;
  it.text = di_strdup(di, title);
  di_push(di, it);
}

void debug_inspector_section_end(DebugInspector* di) {
  DebugItem it = {0};
  it.kind = DBG_ITEM_SECTION_END;
  di_push(di, it);
}

void debug_inspector_add_text(DebugInspector* di, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  DebugItem it = {0};
  it.kind = DBG_ITEM_TEXT;
  it.text = di_fmt(di, fmt, args);

  va_end(args);
  di_push(di, it);
}

void debug_inspector_add_kv_text(DebugInspector* di, const char* key, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);

  DebugItem it = {0};
  it.kind = DBG_ITEM_KV_TEXT;
  it.label = di_strdup(di, key);
  it.kv_value_text = di_fmt(di, fmt, args);

  va_end(args);
  di_push(di, it);
}

static void add_value(DebugInspector* di, const char* label, DebugValue v) {
  DebugItem it = {0};
  it.kind = DBG_ITEM_VALUE;
  it.label = di_strdup(di, label);
  it.value = v;
  di_push(di, it);
}

void debug_inspector_add_u32(DebugInspector* di, const char* label, uint32_t x) {
  DebugValue v = { .type = DBG_T_U32, .lanes = 1 };
  v.u32[0] = x;
  add_value(di, label, v);
}

void debug_inspector_add_i32(DebugInspector* di, const char* label, int32_t x) {
  DebugValue v = { .type = DBG_T_I32, .lanes = 1 };
  v.i32[0] = x;
  add_value(di, label, v);
}

void debug_inspector_add_f32(DebugInspector* di, const char* label, float x) {
  DebugValue v = { .type = DBG_T_F32, .lanes = 1 };
  v.f32[0] = x;
  add_value(di, label, v);
}

void debug_inspector_add_vec2(DebugInspector* di, const char* label, float x, float y) {
  DebugValue v = { .type = DBG_T_VEC2, .lanes = 2 };
  v.f32[0] = x; v.f32[1] = y;
  add_value(di, label, v);
}

void debug_inspector_add_vec3(DebugInspector* di, const char* label, float x, float y, float z) {
  DebugValue v = { .type = DBG_T_VEC3, .lanes = 3 };
  v.f32[0] = x; v.f32[1] = y; v.f32[2] = z;
  add_value(di, label, v);
}

void debug_inspector_add_vec4(DebugInspector* di, const char* label, float x, float y, float z, float w) {
  DebugValue v = { .type = DBG_T_VEC4, .lanes = 4 };
  v.f32[0] = x; v.f32[1] = y; v.f32[2] = z; v.f32[3] = w;
  add_value(di, label, v);
}

void debug_inspector_add_ivec2(DebugInspector* di, const char* label, int32_t x, int32_t y) {
  DebugValue v = { .type = DBG_T_IVEC2, .lanes = 2 };
  v.i32[0] = x; v.i32[1] = y;
  add_value(di, label, v);
}

void debug_inspector_add_ivec3(DebugInspector* di, const char* label, int32_t x, int32_t y, int32_t z) {
  DebugValue v = { .type = DBG_T_IVEC3, .lanes = 3 };
  v.i32[0] = x; v.i32[1] = y; v.i32[2] = z;
  add_value(di, label, v);
}

void debug_inspector_add_uvec2(DebugInspector* di, const char* label, uint32_t x, uint32_t y) {
  DebugValue v = { .type = DBG_T_UVEC2, .lanes = 2 };
  v.u32[0] = x; v.u32[1] = y;
  add_value(di, label, v);
}

void debug_inspector_add_uvec3(DebugInspector* di, const char* label, uint32_t x, uint32_t y, uint32_t z) {
  DebugValue v = { .type = DBG_T_UVEC3, .lanes = 3 };
  v.u32[0] = x; v.u32[1] = y; v.u32[2] = z;
  add_value(di, label, v);
}

void debug_inspector_add_gpu_record_u32words(DebugInspector* di, uint32_t header,
                                            uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
  DebugItem it = {0};
  it.kind = DBG_ITEM_GPU_RECORD;
  it.header = header;
  it.w0 = w0; it.w1 = w1; it.w2 = w2; it.w3 = w3;
  di_push(di, it);
}

void debug_inspector_set_pixel_state(DebugInspector* di, const DbgPinnedPixel* s) {
  if (!s) {
    memset(&di->pixel, 0, sizeof(di->pixel));
    return;
  }
  di->pixel = *s; // copy (small, fixed-size)
  if (di->pixel.count > DBG_MAX_RECORDS) di->pixel.count = DBG_MAX_RECORDS;
}

void debug_inspector_add_pixel_panel(DebugInspector* di) {
  DebugItem it = {0};
  it.kind = DBG_ITEM_PIXEL_PANEL;
  di_push(di, it);
}

// ---------------------
// Draw (Clay)
// ---------------------

static void draw_kv_row(DebugInspector* di, const char* id_prefix, uint32_t i,
                        Clay_String left, Clay_String right) {
  (void)id_prefix;

  CLAY(CLAY_IDI("DbgRow", (int)i), {
    .backgroundColor = di->row_bg,
    .cornerRadius = (Clay_CornerRadius){ 4,4,4,4 },
    .layout = {
      .layoutDirection = CLAY_LEFT_TO_RIGHT,
      .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
      .padding = (Clay_Padding){ 8, 8, 6, 6 },
      .childGap = 10,
    },
  }) {
    CLAY(CLAY_IDI("DbgRowL", (int)i), {
      .layout = { .sizing = { .width = CLAY_SIZING_FIXED(140), .height = CLAY_SIZING_FIT(0) } },
    }) {
      CLAY_TEXT(left, CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->val_col }));
    }

    CLAY_TEXT(right, CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->text_col }));
  }
}

static void draw_pixel_panel(DebugInspector* di) {
//   if (!di->pixel.active) {
//     CLAY_TEXT(CLAY_STRING("Pixel Inspector: (inactive)"),
//               CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->val_col }));
//     return;
//   }

  // Header
  CLAY(CLAY_ID("PixelInspectorHeader"), {
    .layout = {
      .layoutDirection = CLAY_LEFT_TO_RIGHT,
      .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
      .childGap = 10,
    },
  }) {
    CLAY_TEXT(CLAY_STRING("Pixel Inspector"),
              CLAY_TEXT_CONFIG({ .fontSize = 18, .textColor = di->text_col }));
    CLAY_TEXT(di_fmt1(di, "(%d, %d)", di->pixel.x, di->pixel.y),
              CLAY_TEXT_CONFIG({ .fontSize = 18, .textColor = di->val_col }));
  }

  if (di->pixel.count == 0) {
    CLAY_TEXT(CLAY_STRING("No events recorded."),
              CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->val_col }));
    return;
  }

  // Rows: event/key/value
  for (uint32_t i = 0; i < di->pixel.count; i++) {
    DbgRecord* r = &di->pixel.records[i];
    DebugValue v = value_from_gpu_words(r->header, r->y, r->z, r->w, 0);
    Clay_String val = format_value(di, &v);

    CLAY(CLAY_IDI("PixRow", (int)i), {
      .backgroundColor = di->row_bg,
      .cornerRadius = (Clay_CornerRadius){ 4,4,4,4 },
      .layout = {
        .layoutDirection = CLAY_LEFT_TO_RIGHT,
        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(0) },
        .padding = (Clay_Padding){ 8, 8, 6, 6 },
        .childGap = 10,
      },
    }) {
      CLAY(CLAY_IDI("PixEventCol", (int)i), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIT(0) } },
      }) {
        CLAY_TEXT(di_fmt1(di, "%u", DBG_GET_EVENT(r->header)),
                  CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->text_col }));
      }

      CLAY(CLAY_IDI("PixKeyCol", (int)i), {
        .layout = { .sizing = { .width = CLAY_SIZING_FIXED(40), .height = CLAY_SIZING_FIT(0) } },
      }) {
        CLAY_TEXT(di_fmt1(di, "%u", DBG_GET_KEY(r->header)),
                  CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->val_col }));
      }

      CLAY_TEXT(val, CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->text_col }));
    }
  }
}

void debug_inspector_draw(DebugInspector* di) {
  if (!di->open) return;
  Clay_BeginLayout();
  CLAY(CLAY_ID("DebugInspectorPanel"), {
    .backgroundColor = di->panel_bg,
    .cornerRadius = (Clay_CornerRadius){ 8,8,8,8 },
    .layout = {
      .layoutDirection = CLAY_TOP_TO_BOTTOM,
      .sizing = { .width = CLAY_SIZING_FIXED(420), .height = CLAY_SIZING_FIT(0) },
      .padding = CLAY_PADDING_ALL(16),
      .childGap = 10,
    },
  }) {
    // Title
    CLAY_TEXT(CLAY_STRING("Debug Inspector"),
              CLAY_TEXT_CONFIG({ .fontSize = 22, .textColor = di->text_col }));

    for (uint32_t i = 0; i < di->item_count; i++) {
      DebugItem* it = &di->items[i];

      switch (it->kind) {
        case DBG_ITEM_SECTION_BEGIN: {
          CLAY_TEXT(it->text,
                    CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = di->val_col }));
        } break;

        case DBG_ITEM_SECTION_END: {
          // spacing
          CLAY_TEXT(CLAY_STRING(""),
                    CLAY_TEXT_CONFIG({ .fontSize = 6, .textColor = (Clay_Color){0,0,0,0} }));
        } break;

        case DBG_ITEM_TEXT: {
          CLAY_TEXT(it->text,
                    CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = di->text_col }));
        } break;

        case DBG_ITEM_KV_TEXT: {
          draw_kv_row(di, "kv", i, it->label, it->kv_value_text);
        } break;

        case DBG_ITEM_VALUE: {
          Clay_String rhs = format_value(di, &it->value);
          draw_kv_row(di, "val", i, it->label, rhs);
        } break;

        case DBG_ITEM_GPU_RECORD: {
          // Format: "E:K" on left, decoded value on right
          uint32_t ev = DBG_GET_EVENT(it->header);
          uint32_t key = DBG_GET_KEY(it->header);

          Clay_String lhs = di_fmt1(di, "E%u K%u", ev, key);

          DebugValue v = value_from_gpu_words(it->header, it->w0, it->w1, it->w2, it->w3);
          Clay_String rhs = format_value(di, &v);

          draw_kv_row(di, "rec", i, lhs, rhs);
        } break;

        case DBG_ITEM_PIXEL_PANEL: {
          draw_pixel_panel(di);
        } break;

        default: break;
      }
    }
  }
}
