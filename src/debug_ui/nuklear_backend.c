#include "command.h"
#include "common.h"
#include "resource/resmanager.h"
#include "util.h"
#include "window.h"

#include "nuklear_backend.h"
#include <assert.h>

#define NK_ASSERT assert 
#define NK_MEMSET memset
#define NK_GLFW_VULKAN_IMPLEMENTATION

#include  "thirdparty/nuklear/nuklear_glfw_vulkan.h"



// --- Private Prototypes ---

NuklearBackend nuklear_backend_init(M_GPU *dev, M_Resource *rm, TWindow *window) {
  VkImageView *views = calloc(sizeof(VkImageView), window->swapchain.image_count);
  for (u32 i = 0; i < window->swapchain.image_count; i++) {
    views[i] = rm_get_image(rm, window->swapchain.images[i])->view;
  }
  

  struct nk_context *ctx =
      nk_glfw3_init(window->raw_window, dev->device, dev->physical_device, dev->graphics_family, views,
                    window->swapchain.image_count, window->swapchain.format, NK_GLFW3_INSTALL_CALLBACKS, 512, 512);

  struct nk_font_atlas *atlas;
  nk_glfw3_font_stash_begin(&atlas);

  struct nk_font *droid = nk_font_atlas_add_from_file(atlas, "assets/ttf/16020_FUTURAM.ttf", 14, 0);

     
         
         nk_glfw3_font_stash_end(dev->graphics_queue);
         nk_style_set_font(ctx, &droid->handle);

  VkSemaphore wait_semp = vk_create_semp_binary(dev->device, "Semaphore-Nuklear-Wait");

  return (NuklearBackend){.ctx = ctx, .wait_semp = wait_semp};
}

VkSemaphore nuklear_backend_get_wait_binary(NuklearBackend *ctx) { return ctx->wait_semp; }

void nuklear_backend_new_frame(NuklearBackend *ctx) { nk_glfw3_new_frame(); }

struct nk_context *nuklear_backend_get_draw_ctx(NuklearBackend *self) { return self->ctx; }

VkSemaphore nuklear_backend_render(NuklearBackend *ctx, TWindow *window, M_GPU *dev) {
  return nk_glfw3_render(dev->graphics_queue, window->swapchain.current_img_idx, ctx->wait_semp, NK_ANTI_ALIASING_OFF);
}

// --- Private Functions ---
