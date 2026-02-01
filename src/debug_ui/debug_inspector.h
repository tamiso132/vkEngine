
#pragma once
#include "nuklear_backend.h"


#include "thirdparty/nuklear/nuklear.h"



/* ---------------- metadata types ---------------- */
typedef enum editor_pixel_value_type {
  EDITOR_PV_U32,
  EDITOR_PV_I32,
  EDITOR_PV_F32,
  EDITOR_PV_VEC2,
  EDITOR_PV_VEC3
} editor_pixel_value_type;

typedef struct editor_pixel_value {
  const char *key; /* label (caller-owned) */
  editor_pixel_value_type type;
  union {
    unsigned int u32;
    int i32;
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

typedef struct editor_pixel_editor {
  int w, h;

  /* Optional preview buffer: w*h packed RGBA.
     Assumed 0xAARRGGBB (change editor_rgba_to_nk if needed). */
  unsigned int *rgba;

  /* Per-pixel metadata (dense). */
  editor_pixel_meta *meta; /* w*h */

  /* Selection */
  int sel_x, sel_y; /* -1 if none */

  /* View */
  float zoom;      /* pixel size in screen units */
  vec2 pan; /* screen-space offset inside canvas */

  /* UI options */
  int show_grid;
} editor_pixel_editor;

// PUBLIC FUNCTIONS
void editor_pixel_editor_append_f32(editor_pixel_editor *ed, int x, int y, const char *key, float v);
void editor_pixel_editor_append_i32(editor_pixel_editor *ed, int x, int y, const char *key, int v);
void editor_pixel_editor_append_u32(editor_pixel_editor *ed, int x, int y, const char *key, unsigned int v);
void editor_pixel_editor_append_vec2(editor_pixel_editor *ed, int x, int y, const char *key, vec2 v);
void editor_pixel_editor_append_vec3(editor_pixel_editor *ed, int x, int y, const char *key, vec3 v);
void editor_pixel_editor_clear_pixel(editor_pixel_editor *ed, int x, int y);

void editor_pixel_editor_free(editor_pixel_editor *ed);
void editor_pixel_editor_init(editor_pixel_editor *ed, int w, int h);
void editor_pixel_editor_ui(editor_pixel_editor *ed, struct nk_context *ctx);
void editor_pixel_editor_window_ui(editor_pixel_editor *ed, struct nk_context *ctx, const char *title, struct nk_rect r,
                                   nk_flags flags);
void editor_pixel_meta_main_append_test_data(editor_pixel_editor *ed);
void editor_pixel_meta_main_draw(editor_pixel_editor *ed, struct nk_context *ctx);
void editor_pixel_meta_main_init(editor_pixel_editor *ed, int w, int h, unsigned int *rgba_or_null);
void editor_pixel_meta_main_shutdown(editor_pixel_editor *ed);

// END PUBLIC FUNCTIONS
