
/* pixel_meta_ui.c  (drop-in, rect-driven Nuklear inspector)
   ---------------------------------------------------------
   - Stores metadata per pixel for a w*h image (game buffer).
   - Renders a pixel canvas + inspector, but CANVAS placement is driven by a rect you pass in.
   - No backend code (no Vulkan/GL/SDL). Pure Nuklear draw commands.

   Key idea:
     - Pixel coordinates are ALWAYS in [0..img_w-1, 0..img_h-1]
     - UI rectangles are screen-space and ONLY used for drawing/picking.

   Notes:
     - Requires Nuklear + your debug_inspector.h types:
         EditorPixelEditor, editor_pixel_meta, editor_pixel_value, EDITOR_PV_* enums.
     - Assumes RGBA preview buffer format: 0xAARRGGBB

   Public API:
     editor_pixel_editor_init(ed, img_w, img_h, rgba_or_null);
     editor_pixel_editor_free(ed);

     editor_pixel_editor_clear_pixel(ed, x, y);
     editor_pixel_editor_append_u32/i32/f32/vec2/vec3(...)

     editor_pixel_editor_ui(ed, ctx, canvas_rect);   // draws canvas in canvas_rect, inspector via Nuklear layout
*/

#include "debug_inspector.h"
#include "cglm/vec2.h"
#include "cglm/vec3.h"
#include "panel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nuklear_config.h"

// --- Private Prototypes ---
static void draw_canvas_rect(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds);
static void draw_inspector(EditorPixelEditor *ed, struct nk_context *ctx);

static void handle_pan(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds);
static void handle_zoom(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds);

static int in_bounds(const EditorPixelEditor *ed, int x, int y);

static editor_pixel_meta *meta_at(EditorPixelEditor *ed, int x, int y);

static int pick_pixel(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds, int *out_x, int *out_y);

static void pixelmeta_push(editor_pixel_meta *m, editor_pixel_value v);

static struct nk_color rgba_to_nk(unsigned int c);

/* ----------------------- public API ----------------------- */

void editor_pixel_editor_init(EditorPixelEditor *ed, int img_w, int img_h, unsigned int *rgba_or_null) {
  memset(ed, 0, sizeof(*ed));

  ed->img_w = img_w;
  ed->img_h = img_h;

  ed->meta = (editor_pixel_meta *)calloc((size_t)img_w * (size_t)img_h, sizeof(editor_pixel_meta));

  ed->sel_x = ed->sel_y = -1;
  ed->zoom = 12.0f;
  ed->pan[0] = 0.0f;
  ed->pan[1] = 0.0f;
  ed->show_grid = 1;
}

void editor_pixel_editor_free(EditorPixelEditor *ed) {
  if (!ed)
    return;
  if (ed->meta) {
    int n = ed->img_w * ed->img_h;
    for (int i = 0; i < n; ++i)
      free(ed->meta[i].items);
    free(ed->meta);
    ed->meta = NULL;
  }
}

void editor_pixel_editor_clear_pixel(EditorPixelEditor *ed, int x, int y) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_meta *m = meta_at(ed, x, y);
  m->count = 0;
}

void editor_pixel_editor_append_u32(EditorPixelEditor *ed, int x, int y, const char *key, unsigned int v) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_U32;
  pv.as.u32 = v;
  pixelmeta_push(meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_i32(EditorPixelEditor *ed, int x, int y, const char *key, int v) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_I32;
  pv.as.i32 = v;
  pixelmeta_push(meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_f32(EditorPixelEditor *ed, int x, int y, const char *key, float v) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_F32;
  pv.as.f32 = v;
  pixelmeta_push(meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_vec2(EditorPixelEditor *ed, int x, int y, const char *key, vec2 v) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_VEC2;
  glm_vec2_copy(v, pv.as.v2);
  pixelmeta_push(meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_vec3(EditorPixelEditor *ed, int x, int y, const char *key, vec3 v) {
  if (!in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_VEC3;
  glm_vec3_copy(v, pv.as.v3);
  pixelmeta_push(meta_at(ed, x, y), pv);
}

/* UI: canvas rect is screen-space; inspector uses Nuklear layout (right column) */
void editor_pixel_editor_ui(EditorPixelEditor *ed, struct nk_context *ctx, struct WindowRect inspector_rect) {

  struct nk_rect win = (struct nk_rect){
    .x = (float)inspector_rect.offset.x,
    .y = (float)inspector_rect.offset.y,
    .w = (float)inspector_rect.size.width,
    .h = (float)inspector_rect.size.height
  };

  if (nk_begin(ctx, "Debug", win, NK_WINDOW_NO_SCROLLBAR)) {

    if (nk_group_begin(ctx, "Canvas", NK_WINDOW_BORDER)) {
      /* draw the canvas exactly where you want */
      struct nk_rect canvas_rect = nk_widget_bounds(ctx);
      draw_canvas_rect(ed, ctx, canvas_rect);
      nk_group_end(ctx);
    }

    if (nk_group_begin(ctx, "Inspector", NK_WINDOW_BORDER)) {
      draw_inspector(ed, ctx);
      nk_group_end(ctx);
    }
    nk_end(ctx);
  }
}

void editor_pixel_editor_set_selected(EditorPixelEditor *ed, int x, int y) {
  if (!ed)
    return;
  if (x < 0 || y < 0 || x >= ed->img_w || y >= ed->img_h) {
    ed->sel_x = ed->sel_y = -1;
    return;
  }
  ed->sel_x = x;
  ed->sel_y = y;
}

// --- Private Functions ---

/* ----------------------- drawing ----------------------- */
static void draw_canvas_rect(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds) {
  struct nk_command_buffer *out = nk_window_get_canvas(ctx);

  /* save/restore clip: nk_push_scissor overwrites b->clip (no stack) */
  struct nk_rect old_clip = out->clip;
  nk_push_scissor(out, bounds);

  nk_fill_rect(out, bounds, 0.0f, nk_rgb(22, 22, 22));

  handle_pan(ed, ctx, bounds);
  handle_zoom(ed, ctx, bounds);

  int px, py;
  if (pick_pixel(ed, ctx, bounds, &px, &py)) {
    ed->sel_x = px;
    ed->sel_y = py;
  }

  const float cell = ed->zoom;
  float ox = bounds.x + ed->pan[0];
  float oy = bounds.y + ed->pan[1];

  /* compute visible pixel range, then clamp to image */
  int x0 = (int)floorf((bounds.x - ox) / cell) - 1;
  int y0 = (int)floorf((bounds.y - oy) / cell) - 1;
  int x1 = (int)ceilf(((bounds.x + bounds.w) - ox) / cell) + 1;
  int y1 = (int)ceilf(((bounds.y + bounds.h) - oy) / cell) + 1;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > ed->img_w)
    x1 = ed->img_w;
  if (y1 > ed->img_h)
    y1 = ed->img_h;

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      struct nk_rect r = {ox + x * cell, oy + y * cell, cell, cell};

      struct nk_color col = nk_rgb(45, 45, 45);

      nk_fill_rect(out, r, 0.0f, col);

      if (ed->show_grid) {
        nk_stroke_rect(out, r, 0.0f, 1.0f, nk_rgb(15, 15, 15));
      }
    }
  }

  if (in_bounds(ed, ed->sel_x, ed->sel_y)) {
    struct nk_rect s = {ox + ed->sel_x * cell, oy + ed->sel_y * cell, cell, cell};
    nk_stroke_rect(out, s, 0.0f, 2.0f, nk_rgb(255, 230, 80));
  }

  /* HUD */
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "zoom: %.1f  pan: (%.0f, %.0f)", ed->zoom, ed->pan[0], ed->pan[1]);
    struct nk_rect hud = {bounds.x + 8, bounds.y + 8, bounds.w - 16, 18};
    nk_draw_text(out, hud, buf, (int)strlen(buf), ctx->style.font, nk_rgba(0, 0, 0, 120), nk_rgb(220, 220, 220));
  }

  nk_push_scissor(out, old_clip);
}

static void draw_inspector(EditorPixelEditor *ed, struct nk_context *ctx) {
  nk_layout_row_dynamic(ctx, 24, 1);

  if (!in_bounds(ed, ed->sel_x, ed->sel_y)) {
    nk_label(ctx, "Click a pixel in the canvas to inspect.", NK_TEXT_LEFT);
    return;
  }

  char hdr[128];
  snprintf(hdr, sizeof(hdr), "Pixel (%d, %d)", ed->sel_x, ed->sel_y);
  nk_label(ctx, hdr, NK_TEXT_LEFT);

  editor_pixel_meta *m = meta_at(ed, ed->sel_x, ed->sel_y);

  nk_layout_row_dynamic(ctx, 22, 2);
  if (nk_button_label(ctx, "Clear pixel"))
    editor_pixel_editor_clear_pixel(ed, ed->sel_x, ed->sel_y);
  nk_checkbox_label(ctx, "Grid", &ed->show_grid);

  nk_layout_row_dynamic(ctx, 18, 1);
  nk_label(ctx, "Metadata:", NK_TEXT_LEFT);

  if (m->count == 0) {
    nk_label(ctx, "(none)", NK_TEXT_LEFT);
    return;
  }

  for (unsigned int i = 0; i < m->count; ++i) {
    editor_pixel_value *v = &m->items[i];
    const char *key = v->key ? v->key : "(unnamed)";
    char line[256];

    switch (v->type) {
    case EDITOR_PV_U32:
      snprintf(line, sizeof(line), "%s: u32  %u", key, v->as.u32);
      break;
    case EDITOR_PV_I32:
      snprintf(line, sizeof(line), "%s: i32  %d", key, v->as.i32);
      break;
    case EDITOR_PV_F32:
      snprintf(line, sizeof(line), "%s: f32  %.7g", key, v->as.f32);
      break;
    case EDITOR_PV_VEC2:
      snprintf(line, sizeof(line), "%s: vec2 (%.7g, %.7g)", key, v->as.v2[0], v->as.v2[1]);
      break;
    case EDITOR_PV_VEC3:
      snprintf(line, sizeof(line), "%s: vec3 (%.7g, %.7g, %.7g)", key, v->as.v3[0], v->as.v3[1], v->as.v3[2]);
      break;
    default:
      snprintf(line, sizeof(line), "%s: (unknown)", key);
      break;
    }

    nk_layout_row_dynamic(ctx, 18, 1);
    nk_label(ctx, line, NK_TEXT_LEFT);
  }
}

/* ----------------------- input ----------------------- */
static void handle_pan(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds) {
  if (nk_input_is_mouse_down(&ctx->input, NK_BUTTON_MIDDLE) && nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
    ed->pan[0] += ctx->input.mouse.delta.x;
    ed->pan[1] += ctx->input.mouse.delta.y;
  }
}

static void handle_zoom(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds) {
  if (!nk_input_is_mouse_hovering_rect(&ctx->input, bounds))
    return;

  float scroll = ctx->input.mouse.scroll_delta.y;
  if (scroll == 0.0f)
    return;

  float old = ed->zoom;
  float nz = old + scroll * 1.0f;
  if (nz < 2.0f)
    nz = 2.0f;
  if (nz > 60.0f)
    nz = 60.0f;

  /* zoom around mouse */
  float mx = ctx->input.mouse.pos.x - bounds.x;
  float my = ctx->input.mouse.pos.y - bounds.y;

  float cx = (mx - ed->pan[0]) / old;
  float cy = (my - ed->pan[1]) / old;

  ed->zoom = nz;
  ed->pan[0] = mx - cx * nz;
  ed->pan[1] = my - cy * nz;
}

/* ----------------------- helpers ----------------------- */
static int in_bounds(const EditorPixelEditor *ed, int x, int y) {
  return (x >= 0 && y >= 0 && x < ed->img_w && y < ed->img_h);
}

static editor_pixel_meta *meta_at(EditorPixelEditor *ed, int x, int y) { return &ed->meta[y * ed->img_w + x]; }

static int pick_pixel(EditorPixelEditor *ed, struct nk_context *ctx, struct nk_rect bounds, int *out_x, int *out_y) {
  if (!nk_input_is_mouse_click_down_in_rect(&ctx->input, NK_BUTTON_LEFT, bounds, nk_true))
    return 0;

  float mx = ctx->input.mouse.pos.x - bounds.x - ed->pan[0];
  float my = ctx->input.mouse.pos.y - bounds.y - ed->pan[1];

  int x = (int)(mx / ed->zoom);
  int y = (int)(my / ed->zoom);

  if (!in_bounds(ed, x, y))
    return 0;

  *out_x = x;
  *out_y = y;
  return 1;
}

static void pixelmeta_push(editor_pixel_meta *m, editor_pixel_value v) {
  if (m->count == m->cap) {
    unsigned int new_cap = m->cap ? (m->cap * 2u) : 4u;
    editor_pixel_value *new_items =
        (editor_pixel_value *)realloc(m->items, (size_t)new_cap * sizeof(editor_pixel_value));
    if (!new_items)
      return; /* OOM: drop */
    m->items = new_items;
    m->cap = new_cap;
  }
  m->items[m->count++] = v;
}

static struct nk_color rgba_to_nk(unsigned int c) {
  /* Assumes 0xAARRGGBB */
  unsigned char a = (unsigned char)((c >> 24) & 0xFF);
  unsigned char r = (unsigned char)((c >> 16) & 0xFF);
  unsigned char g = (unsigned char)((c >> 8) & 0xFF);
  unsigned char b = (unsigned char)((c >> 0) & 0xFF);
  return nk_rgba(r, g, b, a);
}
