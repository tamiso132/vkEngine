#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <clay.h>
#include "shaders/ui_shared.glsl" // GPUClayVertex
#include "command.h"
#include <stb_truetype.h>
// Forward decl for stb struct without exposing stb header in every file.

typedef struct UI_FontAtlas {
  float bake_px;                 // size used at bake time
  int w, h;                      // atlas dims
  stbtt_bakedchar* baked;        // 96 entries for ASCII 32..126
  ResHandle texture;             // uploaded GPU texture (RGBA8)
  bool valid;
} UI_FontAtlas;

// Bake ASCII 32..126 into an atlas and upload to GPU.
// - texture format: VK_FORMAT_R8G8B8A8_UNORM (alpha replicated into all channels)
// - returns true on success

bool ui_fontatlas_init_from_ttf_file(UI_FontAtlas* fa,
                                     const char* ttf_path,
                                     float bake_px,
                                     int atlas_w,
                                     int atlas_h,
                                     CmdBuffer upload_cmd,
                                     M_Resource* rm);

void ui_fontatlas_shutdown(UI_FontAtlas* fa);

Clay_Dimensions ui_clay_measure_text_stb(Clay_StringSlice text,
                                        Clay_TextElementConfig* config,
                                        void* userData);

uint32_t ui_clay_emit_text(const UI_FontAtlas* fa,
                           const Clay_TextRenderData* td,
                           Clay_BoundingBox box,
                           GPUClayVertex* vtx,
                           uint16_t* idx,
                           uint32_t* v_off,
                           uint32_t* i_off);
