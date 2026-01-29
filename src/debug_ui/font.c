#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

// ----------------------------------
// Small file loader
// ----------------------------------
static bool read_file_bytes(const char* path, uint8_t** out_data, size_t* out_size) {
  *out_data = NULL; *out_size = 0;
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) { fclose(f); return false; }
  uint8_t* data = (uint8_t*)malloc((size_t)sz);
  if (!data) { fclose(f); return false; }
  if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
    fclose(f); free(data); return false;
  }
  fclose(f);
  *out_data = data;
  *out_size = (size_t)sz;
  return true;
}

static inline void emit_quad(GPUClayVertex* vtx, uint16_t* idx,
                             uint32_t* v_off, uint32_t* i_off,
                             float x0, float y0, float x1, float y1,
                             float u0, float v0_, float u1, float v1_,
                             float r, float g, float b, float a)
{
  uint32_t v = *v_off;
  uint32_t i = *i_off;

  idx[i + 0] = (uint16_t)(v + 0);
  idx[i + 1] = (uint16_t)(v + 1);
  idx[i + 2] = (uint16_t)(v + 2);
  idx[i + 3] = (uint16_t)(v + 2);
  idx[i + 4] = (uint16_t)(v + 3);
  idx[i + 5] = (uint16_t)(v + 0);

  vtx[v + 0] = (GPUClayVertex){{x0, y0}, {u0, v0_}};
  vtx[v + 1] = (GPUClayVertex){{x1, y0}, {u1, v0_}};
  vtx[v + 2] = (GPUClayVertex){{x1, y1}, {u1, v1_}};
  vtx[v + 3] = (GPUClayVertex){{x0, y1}, {u0, v1_}};

  vtx[v + 0].color[0] = r; vtx[v + 0].color[1] = g; vtx[v + 0].color[2] = b; vtx[v + 0].color[3] = a;
  vtx[v + 1].color[0] = r; vtx[v + 1].color[1] = g; vtx[v + 1].color[2] = b; vtx[v + 1].color[3] = a;
  vtx[v + 2].color[0] = r; vtx[v + 2].color[1] = g; vtx[v + 2].color[2] = b; vtx[v + 2].color[3] = a;
  vtx[v + 3].color[0] = r; vtx[v + 3].color[1] = g; vtx[v + 3].color[2] = b; vtx[v + 3].color[3] = a;

  *v_off += 4;
  *i_off += 6;
}

bool ui_fontatlas_init_from_ttf_file(UI_FontAtlas* fa,
                                     const char* ttf_path,
                                     float bake_px,
                                     int atlas_w,
                                     int atlas_h,
                                     CmdBuffer upload_cmd,
                                     M_Resource* rm)
{
  memset(fa, 0, sizeof(*fa));
  fa->bake_px = bake_px;
  fa->w = atlas_w;
  fa->h = atlas_h;

  uint8_t* ttf = NULL;
  size_t ttf_sz = 0;
  if (!read_file_bytes(ttf_path, &ttf, &ttf_sz)) {
    printf("[UI] Failed to read font: %s\n", ttf_path);
    return false;
  }

  uint8_t* alpha = (uint8_t*)malloc((size_t)atlas_w * (size_t)atlas_h);
  if (!alpha) { free(ttf); return false; }
  memset(alpha, 0, (size_t)atlas_w * (size_t)atlas_h);
stbtt_bakedchar d;
  // allocate baked array
  fa->baked = (stbtt_bakedchar*)malloc(sizeof(stbtt_bakedchar) * 96);
  if (!fa->baked) { free(ttf); free(alpha); return false; }

  int ok = stbtt_BakeFontBitmap(ttf, 0, bake_px,
                                alpha, atlas_w, atlas_h,
                                32, 96, (stbtt_bakedchar*)fa->baked);
  free(ttf);

  if (ok <= 0) {
    printf("[UI] stbtt_BakeFontBitmap failed\n");
    free(alpha);
    free(fa->baked); fa->baked = NULL;
    return false;
  }

  // expand alpha -> RGBA (same value in all channels so you can sample .r or .a)
  size_t rgba_sz = (size_t)atlas_w * (size_t)atlas_h * 4;
  uint8_t* rgba = (uint8_t*)malloc(rgba_sz);
  if (!rgba) {
    free(alpha);
    free(fa->baked); fa->baked = NULL;
    return false;
  }

  for (int y = 0; y < atlas_h; y++) {
    for (int x = 0; x < atlas_w; x++) {
      uint8_t a = alpha[y * atlas_w + x];
      size_t o = ((size_t)y * (size_t)atlas_w + (size_t)x) * 4;
      rgba[o + 0] = a;
      rgba[o + 1] = a;
      rgba[o + 2] = a;
      rgba[o + 3] = a;
    }
  }
  free(alpha);

  // create GPU image + upload through your resource manager staging
  RGImageInfo img = {
    .name   = "UI_FontAtlas",
    .width  = (uint32_t)atlas_w,
    .height = (uint32_t)atlas_h,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .usage  = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    .preset = RG_IMAGETYPE_TEXTURE
  };
  fa->texture = rm_create_image(rm, img);

  RmStageSlice slice = rm_get_stage_buffer(rm, rgba, (uint32_t)rgba_sz, 4);
  free(rgba);

  cmd_sync_image(upload_cmd, rm, fa->texture, STATE_TRANSFER, ACCESS_WRITE);

  RImage* ri = rm_get_image(rm, fa->texture);
  VkBufferImageCopy region = {
    .bufferOffset = slice.offset,
    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
    .imageExtent = {(uint32_t)atlas_w, (uint32_t)atlas_h, 1}
  };
  vkCmdCopyBufferToImage(upload_cmd.buffer, slice.buffer, ri->handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  cmd_sync_image(upload_cmd, rm, fa->texture, STATE_SHADER, ACCESS_READ);

  fa->valid = true;
  return true;
}

void ui_fontatlas_shutdown(UI_FontAtlas* fa) {
  if (!fa) return;
  if (fa->baked) {
    free(fa->baked);
    fa->baked = NULL;
  }
  fa->valid = false;
}

Clay_Dimensions ui_clay_measure_text_stb(Clay_StringSlice text,
                                        Clay_TextElementConfig* config,
                                        void* userData)
{
  UI_FontAtlas* fa = (UI_FontAtlas*)userData;
  if (!fa || !fa->valid || !fa->baked) {
    // fallback: non-zero width so you can see something
    float w = (float)text.length * (float)config->fontSize * 0.6f;
    float h = (config->lineHeight != 0) ? (float)config->lineHeight : (float)config->fontSize;
    return (Clay_Dimensions){ w, h };
  }

  float scale = (float)config->fontSize / fa->bake_px;
  float width = 0.0f;

  const unsigned char* s = (const unsigned char*)text.chars;
  for (uint32_t i = 0; i < (uint32_t)text.length; i++) {
    unsigned char c = s[i];
    if (c < 32 || c > 126) continue;
    const stbtt_bakedchar* bc = &((const stbtt_bakedchar*)fa->baked)[c - 32];
    width += (bc->xadvance * scale) + (float)config->letterSpacing;
  }

  float height = (config->lineHeight != 0) ? (float)config->lineHeight : (float)config->fontSize;
  return (Clay_Dimensions){ width, height };
}

uint32_t ui_clay_emit_text(const UI_FontAtlas* fa,
                           const Clay_TextRenderData* td,
                           Clay_BoundingBox box,
                           GPUClayVertex* vtx,
                           uint16_t* idx,
                           uint32_t* v_off,
                           uint32_t* i_off)
{
  if (!fa || !fa->valid || !fa->baked || !td) return 0;

  // Clay text config
  float fontSize = (float)td->fontSize;
  float letterSpacing = (float)td->letterSpacing;
  float scale = fontSize / fa->bake_px;

  float r = td->textColor.r / 255.f;
  float g = td->textColor.g / 255.f;
  float b = td->textColor.b / 255.f;
  float a = td->textColor.a / 255.f;

  // stb pen coords in baked px space
  float pen_x = box.x / scale;
  float pen_y = (box.y + fontSize) / scale;

  const unsigned char* s = (const unsigned char*)td->stringContents.chars;
  uint32_t start_i = *i_off;

  for (uint32_t i = 0; i < (uint32_t)td->stringContents.length; i++) {
    unsigned char c = s[i];
    if (c < 32 || c > 126) continue;

    stbtt_aligned_quad q;
    stbtt_GetBakedQuad((stbtt_bakedchar*)fa->baked,
                       fa->w, fa->h,
                       (int)(c - 32),
                       &pen_x, &pen_y,
                       &q, 1);

    float x0 = q.x0 * scale;
    float y0 = q.y0 * scale;
    float x1 = q.x1 * scale;
    float y1 = q.y1 * scale;

    emit_quad(vtx, idx, v_off, i_off,
              x0, y0, x1, y1,
              q.s0, q.t0, q.s1, q.t1,
              r, g, b, a);

    // Clay spacing is in screen px; convert to baked space increment
    pen_x += letterSpacing / scale;
  }

  return (*i_off - start_i);
}