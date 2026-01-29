#pragma once

#include "command.h"
#include "common.h"
#include "window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>



typedef struct NuklearBackend {
  struct nk_context *ctx;
} NuklearBackend;

NuklearBackend nuklear_backend_init(M_GPU *dev, M_Resource *rm, TWindow *window);
void nuklear_backend_new_frame(NuklearBackend *ctx);
void nuklear_backend_render(NuklearBackend *ctx, CmdBuffer cmd, M_GPU *dev, TWindow *window);

struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self);