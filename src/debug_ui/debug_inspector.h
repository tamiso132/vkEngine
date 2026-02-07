
#pragma once
#include "nuklear_backend.h"

#include "panel.h"
#include "thirdparty/nuklear/nuklear.h"

/* ---------------- metadata types ---------------- */
typedef enum editor_pixel_value_type {
  EDITOR_PV_U32 = 0,
  EDITOR_PV_I32,
  EDITOR_PV_F32,
  EDITOR_PV_VEC2,
  EDITOR_PV_VEC3
} editor_pixel_value_type;

typedef struct editor_pixel_value {
  const char *key; /* caller-owned string lifetime */
  editor_pixel_value_type type;
  union {
    uint32_t u32;
    int32_t i32;
    float f32;
    vec2 v2;
    vec3 v3;
  } as;
} editor_pixel_value;

typedef struct editor_pixel_meta {
  editor_pixel_value *items;
  unsigned int count;
  unsigned int cap;
} editor_pixel_meta;

/* ----------- main editor ----------- */

typedef struct EditorPixelEditor {
  /* inspected pixel buffer size */
  int img_w, img_h;

  /* per-pixel metadata array: img_w * img_h */
  editor_pixel_meta *meta;

  /* selection in pixel coordinates */
  int sel_x, sel_y;

  /* canvas view controls */
  float zoom;
  float pan[2];
  int show_grid;

} EditorPixelEditor;

// PUBLIC FUNCTIONS
void editor_pixel_editor_append_f32(EditorPixelEditor *ed, int x, int y, const char *key, float v);
void editor_pixel_editor_append_i32(EditorPixelEditor *ed, int x, int y, const char *key, int v);
void editor_pixel_editor_append_u32(EditorPixelEditor *ed, int x, int y, const char *key, unsigned int v);
void editor_pixel_editor_append_vec2(EditorPixelEditor *ed, int x, int y, const char *key, vec2 v);
void editor_pixel_editor_append_vec3(EditorPixelEditor *ed, int x, int y, const char *key, vec3 v);
void editor_pixel_editor_clear_pixel(EditorPixelEditor *ed, int x, int y);
void editor_pixel_editor_free(EditorPixelEditor *ed);
void editor_pixel_editor_init(EditorPixelEditor *ed, int img_w, int img_h, unsigned int *rgba_or_null);
void editor_pixel_editor_set_selected(EditorPixelEditor *ed, int x, int y);
void editor_pixel_editor_ui(EditorPixelEditor *ed, struct nk_context *ctx, struct WindowRect canvas_rect);
// END PUBLIC FUNCTIONS
