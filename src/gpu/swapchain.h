#pragma once
#include "../vector.h"
#include "common.h"
#include "gpu.h"

#define MAX_FRAMES_IN_FLIGHT 1

typedef struct M_Swapchain {
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;

  u32 image_count;
  u32 current_img_idx; // The index returned by vkAcquireNextImage

  ResHandle *images; // Array of RImage handles (Size: image_count)

  // Acquire Semaphores:
  //     Waited on by the GPU (via sm_work) before rendering.
  //     One per Frame-In-Flight (because we need one for the current CPU frame).
  VkSemaphore sem_acquire[MAX_FRAMES_IN_FLIGHT];

  // Render Finished Semaphores:
  //     Waited on by the Presentation Engine (via sm_present).
  //     One per Swapchain Image (because we present specific images).
  VkSemaphore *sem_render_finished;
} M_Swapchain;

// PUBLIC FUNCTIONS
void swapchain_destroy(M_Swapchain *sc);
ResHandle swapchain_get_image(M_Swapchain *sc);
void swapchain_init(M_Swapchain *sc, VkSurfaceKHR surface, const char *name);

VkSemaphore swapchain_get_render_done_semp(M_Swapchain *sc);
// END PUBLIC FUNCTIONS
