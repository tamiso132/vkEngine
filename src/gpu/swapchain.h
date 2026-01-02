#pragma once
#include "gpu.h"
#include "resmanager.h"
#include "vector.h"

// PUBLIC FUNCTIONS
typedef struct SwapPresent {
  ResHandle handle;
  VkSemaphore sem_rend_done;
} PresentFrame;

typedef struct M_Swapchain {
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;

  VECTOR_TYPES(PresentFrame)
  Vector imgs;

  uint32_t current_img_idx;
} M_Swapchain;

typedef struct GPUSwapchainSystemInfo {
} GPUSwapchainSystemInfo;

ResHandle swapchain_get_image(M_Swapchain *sc);
void swapchain_destroy(GPUDevice *dev, M_Swapchain *sc);
