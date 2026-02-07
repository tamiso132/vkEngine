#pragma once
#include "command.h"
#include "common.h"
#include "panel.h"
#include "raycam.h"
#include "transfer_queue.h"

typedef struct {
  CmdBuffer cmd; // Den aktuella kommandobuffern för denna frame
  float dt;      // Delta time (kan läggas till senare)
  WindowRect game_rect;
  ResHandle swap_img;
  M_Resource *rm;
  M_HotReload *pr;
  M_Pipeline *pm;
  M_GPU *gpu;
  Camera cam;
  TransferQueue *tq;
  u32 readback_idx;
  ivec2 dbg_mouse_pos;

} SampleContext;

// Interface för ett sample
typedef struct Sample {
  const char *name;
  void *user_data;

  void (*init)(struct Sample *self, SampleContext *ctx);
  void (*pre_render)(struct Sample *self, SampleContext *ctx);
  void (*render)(struct Sample *self, SampleContext *ctx);
  void (*post_render)(struct Sample *self, SampleContext *ctx);
  void (*on_resize)(struct Sample *self, SampleContext *ctx);

  void (*destroy)(struct Sample *self);
} Sample;

// Funktion för att köra ett sample (implementeras i sample_runner.c)
