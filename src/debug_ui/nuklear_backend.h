#pragma once

#include "command.h"
#include "common.h"
#include "window.h"

#include "nuklear_config.h"




typedef struct NuklearBackend {
  struct nk_context *ctx;
  VkSemaphore wait_semp;
} NuklearBackend;

// PUBLIC FUNCTIONS
struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self);
VkSemaphore nuklear_backend_get_wait_binary(NuklearBackend *ctx);
NuklearBackend nuklear_backend_init(M_GPU *dev, M_Resource *rm, TWindow *window);
void nuklear_backend_new_frame(NuklearBackend *ctx);
VkSemaphore nuklear_backend_render(NuklearBackend *ctx, TWindow *window, M_GPU *dev);
// END PUBLIC FUNCTIONS
