#pragma once
#include "../vector.h"
#include "gpu.h"

// Remove SYSTEM_DECLARE_ID calls. Swapchain is no longer a singleton system.

typedef struct SwapPresent {
  ResHandle handle;
  VkSemaphore sem_rend_done;
} PresentFrame;

typedef struct M_Swapchain {
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;

  u32 image_count;
  u32 current_img_idx; // Index returned by vkAcquireNextImage

  ResHandle *images; // Array of RImage handles (Size: image_count)

  VkSemaphore *sem_render_finished;
} M_Swapchain;


// PUBLIC FUNCTIONS
void swapchain_destroy(M_Swapchain *sc);
ResHandle swapchain_get_image(M_Swapchain *sc);
void swapchain_init(M_Swapchain *sc, VkSurfaceKHR surface, const char *name);
// END PUBLIC FUNCTIONS

