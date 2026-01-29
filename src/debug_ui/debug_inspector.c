/*  pixel_meta_ui.h  (drop-in UI code)
    ----------------------------------
    You already have Nuklear rendering/input. This file only contains:
      - PixelEditor data + metadata store
      - Append APIs for u32/i32/f32/vec2/vec3
      - Nuklear UI function that draws:
          * left: pixel canvas you can click to select a pixel
          * right: inspector listing all metadata for that pixel
      - No backend code (no SDL/GL/etc). Pure Nuklear.

    Usage:
      1) Create/init PixelEditor:
           PixelEditor pe;
           pe_init(&pe, W, H);
           pe.zoom = 12.0f; // optional
           pe.pan = (struct nk_vec2){0,0};

      2) Each frame in your UI:
           pe_ui(&pe, ctx);

      3) Append metadata whenever you want:
           pe_append_u32(&pe, x, y, "id", 123);
           pe_append_vec3(&pe, x, y, "normal", (vec3){0,1,0});

    Optional:
      - Provide pe.rgba (w*h) if you want pixels colored by a preview buffer.
        Format assumed: 0xAARRGGBB (adjust in pe_rgba_to_nk if yours differs).

    Notes:
      - This draws one filled rect per pixel. Great for 32..256 grids.
      - If you need sparse metadata, tell me and I’ll swap meta[] to a hashmap.
*/

#include "debug_inspector.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Private Prototypes ---
static int editor_in_bounds(const editor_pixel_editor *ed, int x, int y);
static editor_pixel_meta *editor_meta_at(editor_pixel_editor *ed, int x, int y);
static int editor_pick_pixel(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds, int *out_x,
                             int *out_y);
static void editor_pixelmeta_push(editor_pixel_meta *m, editor_pixel_value v);
static struct nk_color editor_rgba_to_nk(unsigned int c);

static void editor_draw_canvas(editor_pixel_editor *ed, struct nk_context *ctx, float canvas_h);
static void editor_draw_inspector(editor_pixel_editor *ed, struct nk_context *ctx);

static void editor_handle_pan(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds);
static void editor_handle_zoom(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds);

/* ---------------- PUBLIC IMPLEMENTATION ---------------- */

void editor_pixel_meta_main_init(editor_pixel_editor *ed, int w, int h, unsigned int *rgba_or_null) {
  editor_pixel_editor_init(ed, w, h);
  ed->rgba = rgba_or_null; /* user-owned */
}

void editor_pixel_meta_main_shutdown(editor_pixel_editor *ed) {
  editor_pixel_editor_free(ed);
  /* ed->rgba is user-owned; don't free it here */
  ed->rgba = NULL;
}

void editor_pixel_meta_main_draw(editor_pixel_editor *ed, struct nk_context *ctx) {
  /* Default window config. Change title/rect/flags if you want. */
  editor_pixel_editor_window_ui(ed, ctx, "Pixel Meta Editor", nk_rect(20, 20, 900, 520),
                                NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE);
}

void editor_pixel_meta_main_append_test_data(editor_pixel_editor *ed) {
  /* Safe even if called multiple times: it appends repeatedly */
  editor_pixel_editor_append_u32(ed, 10, 12, "id", 123u);
  editor_pixel_editor_append_vec2(ed, 10, 12, "uv", (editor_vec2){0.25f, 0.75f});
  editor_pixel_editor_append_vec3(ed, 10, 12, "normal", (editor_vec3){0.0f, 1.0f, 0.0f});
  editor_pixel_editor_append_f32(ed, 1, 1, "depth", 0.42f);
}

void editor_pixel_editor_init(editor_pixel_editor *ed, int w, int h) {
  memset(ed, 0, sizeof(*ed));
  ed->w = w;
  ed->h = h;
  ed->meta = (editor_pixel_meta *)calloc((size_t)w * (size_t)h, sizeof(editor_pixel_meta));
  ed->sel_x = ed->sel_y = -1;
  ed->zoom = 12.0f;
  ed->pan = (editor_vec2){0, 0};
  ed->show_grid = 1;
}

void editor_pixel_editor_free(editor_pixel_editor *ed) {
  if (!ed)
    return;
  if (ed->meta) {
    int n = ed->w * ed->h;
    for (int i = 0; i < n; ++i) {
      free(ed->meta[i].items);
    }
    free(ed->meta);
    ed->meta = NULL;
  }
}

void editor_pixel_editor_clear_pixel(editor_pixel_editor *ed, int x, int y) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_meta *m = editor_meta_at(ed, x, y);
  m->count = 0;
}

void editor_pixel_editor_append_u32(editor_pixel_editor *ed, int x, int y, const char *key, unsigned int v) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_U32;
  pv.as.u32 = v;
  editor_pixelmeta_push(editor_meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_i32(editor_pixel_editor *ed, int x, int y, const char *key, int v) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_I32;
  pv.as.i32 = v;
  editor_pixelmeta_push(editor_meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_f32(editor_pixel_editor *ed, int x, int y, const char *key, float v) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_F32;
  pv.as.f32 = v;
  editor_pixelmeta_push(editor_meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_vec2(editor_pixel_editor *ed, int x, int y, const char *key, editor_vec2 v) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_VEC2;
  pv.as.v2 = v;
  editor_pixelmeta_push(editor_meta_at(ed, x, y), pv);
}

void editor_pixel_editor_append_vec3(editor_pixel_editor *ed, int x, int y, const char *key, editor_vec3 v) {
  if (!editor_in_bounds(ed, x, y))
    return;
  editor_pixel_value pv;
  memset(&pv, 0, sizeof(pv));
  pv.key = key;
  pv.type = EDITOR_PV_VEC3;
  pv.as.v3 = v;
  editor_pixelmeta_push(editor_meta_at(ed, x, y), pv);
}

void editor_pixel_editor_ui(editor_pixel_editor *ed, struct nk_context *ctx) {
  /* two columns inside whatever window/group you put this in */
  nk_layout_row_dynamic(ctx, 0, 2);

  if (nk_group_begin(ctx, "Canvas", NK_WINDOW_BORDER)) {
    editor_draw_canvas(ed, ctx, 10000.0f);
    nk_group_end(ctx);
  }
  if (nk_group_begin(ctx, "Inspector", NK_WINDOW_BORDER)) {
    editor_draw_inspector(ed, ctx);
    nk_group_end(ctx);
  }
}

void editor_pixel_editor_window_ui(editor_pixel_editor *ed, struct nk_context *ctx, const char *title, struct nk_rect r,
                                   nk_flags flags) {
  if (nk_begin(ctx, title, r, flags)) {
    editor_pixel_editor_ui(ed, ctx);
  }
  nk_end(ctx);
}

// --- Private Functions ---

static int editor_in_bounds(const editor_pixel_editor *ed, int x, int y) {
  return (x >= 0 && y >= 0 && x < ed->w && y < ed->h);
}

static editor_pixel_meta *editor_meta_at(editor_pixel_editor *ed, int x, int y) { return &ed->meta[y * ed->w + x]; }

static int editor_pick_pixel(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds, int *out_x,
                             int *out_y) {
  if (!nk_input_is_mouse_click_down_in_rect(&ctx->input, NK_BUTTON_LEFT, bounds, nk_true))
    return 0;

  float mx = ctx->input.mouse.pos.x - bounds.x - ed->pan.x;
  float my = ctx->input.mouse.pos.y - bounds.y - ed->pan.y;

  int px = (int)(mx / ed->zoom);
  int py = (int)(my / ed->zoom);

  if (!editor_in_bounds(ed, px, py))
    return 0;

  *out_x = px;
  *out_y = py;
  return 1;
}

static void editor_pixelmeta_push(editor_pixel_meta *m, editor_pixel_value v) {
  if (m->count == m->cap) {
    unsigned int new_cap = m->cap ? (m->cap * 2u) : 4u;
    editor_pixel_value *new_items = (editor_pixel_value *)realloc(m->items, new_cap * sizeof(editor_pixel_value));
    if (!new_items)
      return; /* OOM: drop */
    m->items = new_items;
    m->cap = new_cap;
  }
  m->items[m->count++] = v;
}

static struct nk_color editor_rgba_to_nk(unsigned int c) {
  /* Assumes 0xAARRGGBB */
  unsigned char a = (unsigned char)((c >> 24) & 0xFF);
  unsigned char r = (unsigned char)((c >> 16) & 0xFF);
  unsigned char g = (unsigned char)((c >> 8) & 0xFF);
  unsigned char b = (unsigned char)((c >> 0) & 0xFF);
  return nk_rgba(r, g, b, a);
}

static void editor_draw_canvas(editor_pixel_editor *ed, struct nk_context *ctx, float canvas_h) {
  nk_layout_row_dynamic(ctx, canvas_h, 1);
  struct nk_rect bounds = nk_widget_bounds(ctx);
  struct nk_command_buffer *out = nk_window_get_canvas(ctx);

  nk_fill_rect(out, bounds, 0.0f, nk_rgb(22, 22, 22));

  editor_handle_pan(ed, ctx, bounds);
  editor_handle_zoom(ed, ctx, bounds);

  int px, py;
  if (editor_pick_pixel(ed, ctx, bounds, &px, &py)) {
    ed->sel_x = px;
    ed->sel_y = py;
  }

  const float cell = ed->zoom;
  float ox = bounds.x + ed->pan.x;
  float oy = bounds.y + ed->pan.y;

  int x0 = (int)floorf((bounds.x - ox) / cell) - 1;
  int y0 = (int)floorf((bounds.y - oy) / cell) - 1;
  int x1 = (int)ceilf(((bounds.x + bounds.w) - ox) / cell) + 1;
  int y1 = (int)ceilf(((bounds.y + bounds.h) - oy) / cell) + 1;

  if (x0 < 0)
    x0 = 0;
  if (y0 < 0)
    y0 = 0;
  if (x1 > ed->w)
    x1 = ed->w;
  if (y1 > ed->h)
    y1 = ed->h;

  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      struct nk_rect r = {ox + x * cell, oy + y * cell, cell, cell};

      struct nk_color col = nk_rgb(45, 45, 45);
      if (ed->rgba)
        col = editor_rgba_to_nk(ed->rgba[y * ed->w + x]);

      nk_fill_rect(out, r, 0.0f, col);

      if (ed->show_grid) {
        nk_stroke_rect(out, r, 0.0f, 1.0f, nk_rgb(15, 15, 15));
      }
    }
  }

  if (editor_in_bounds(ed, ed->sel_x, ed->sel_y)) {
    struct nk_rect s = {ox + ed->sel_x * cell, oy + ed->sel_y * cell, cell, cell};
    nk_stroke_rect(out, s, 0.0f, 2.0f, nk_rgb(255, 230, 80));
  }

  /* HUD */
  {
    char buf[128];
    snprintf(buf, sizeof(buf), "zoom: %.1f  pan: (%.0f, %.0f)", ed->zoom, ed->pan.x, ed->pan.y);
    struct nk_rect hud = {bounds.x + 8, bounds.y + 8, bounds.w - 16, 18};
    nk_draw_text(out, hud, buf, (int)strlen(buf), ctx->style.font, nk_rgba(0, 0, 0, 120), nk_rgb(220, 220, 220));
  }
}

static void editor_draw_inspector(editor_pixel_editor *ed, struct nk_context *ctx) {
  nk_layout_row_dynamic(ctx, 24, 1);

  if (!editor_in_bounds(ed, ed->sel_x, ed->sel_y)) {
    nk_label(ctx, "Click a pixel to inspect.", NK_TEXT_LEFT);
    return;
  }

  char hdr[128];
  snprintf(hdr, sizeof(hdr), "Pixel (%d, %d)", ed->sel_x, ed->sel_y);
  nk_label(ctx, hdr, NK_TEXT_LEFT);

  editor_pixel_meta *m = editor_meta_at(ed, ed->sel_x, ed->sel_y);

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
      snprintf(line, sizeof(line), "%s: vec2 (%.7g, %.7g)", key, v->as.v2.x, v->as.v2.y);
      break;
    case EDITOR_PV_VEC3:
      snprintf(line, sizeof(line), "%s: vec3 (%.7g, %.7g, %.7g)", key, v->as.v3.x, v->as.v3.y, v->as.v3.z);
      break;
    default:
      snprintf(line, sizeof(line), "%s: (unknown)", key);
      break;
    }

    nk_layout_row_dynamic(ctx, 18, 1);
    nk_label(ctx, line, NK_TEXT_LEFT);
  }
}

static void editor_handle_pan(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds) {
  if (nk_input_is_mouse_down(&ctx->input, NK_BUTTON_MIDDLE) && nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
    ed->pan.x += ctx->input.mouse.delta.x;
    ed->pan.y += ctx->input.mouse.delta.y;
  }
}

static void editor_handle_zoom(editor_pixel_editor *ed, struct nk_context *ctx, struct nk_rect bounds) {
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

  float cx = (mx - ed->pan.x) / old;
  float cy = (my - ed->pan.y) / old;

  ed->zoom = nz;
  ed->pan.x = mx - cx * nz;
  ed->pan.y = my - cy * nz;
}
