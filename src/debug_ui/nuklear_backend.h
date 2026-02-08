#pragma once

#include "command.h"
#include "common.h"
#include "panel.h"
#include "window.h"

#include "input.h"

typedef struct NuklearBackend NuklearBackend;

// PUBLIC FUNCTIONS
struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self);
NuklearBackend* nuklear_backend_init(
    M_GPU *dev, M_Resource *rm, CmdBuffer main_cmd,
    TWindow *window, M_HotReload* hotreloader);

void nuklear_backend_new_frame(NuklearBackend *ctx, Input *input);
void nuklear_backend_record(
    NuklearBackend *backend,
    CmdBuffer cmd,
    M_Pipeline *m_pipeline,
    M_Resource *rm,
    ResHandle swap_img,
    WindowRect win,
    M_GPU *dev);
// END PUBLIC FUNCTIONS
