#pragma once
#include "gpu.h"
#include "resmanager.h"

typedef struct SwapPresent {
  ResHandle handle;
  VkSemaphore sem_rend_done;
} PresentFrame;

typedef struct GPUSwapchain {
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;

  Vector imgs;

  uint32_t current_img_idx;
} GPUSwapchain;

bool swapchain_init(GPUDevice *dev, ResourceManager *rm, GPUSwapchain *sc,
                    uint32_t w, uint32_t h);

ResHandle swapchain_get_image(GPUSwapchain *sc);

void swapchain_destroy(GPUDevice *dev, GPUSwapchain *sc);
