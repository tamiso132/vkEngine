#pragma once
#include "gpu.h"
#include "resmanager.h"
#include "vector.h"

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

typedef struct SwapchainConfig {
} SwapchainConfig;

// PUBLIC FUNCTIONS

SystemFunc swapchain_system_get_func();
void swapchain_resize(M_GPU *dev, M_Resource *rm, M_Swapchain *sc, VkExtent2D *extent);
ResHandle swapchain_get_image(M_Swapchain *sc);
