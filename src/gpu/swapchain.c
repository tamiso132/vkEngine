#include "swapchain.h"
#include "common.h"
#include "resmanager.h"
#include <vulkan/vulkan_core.h>

// --- Private Prototypes ---
static bool create_vulkan_swapchain(GPUDevice *dev, GPUSwapchain *sc, VkSwapchainKHR old_swapchain);

bool swapchain_init(GPUDevice *dev, ResourceManager *rm, GPUSwapchain *sc, uint32_t *w, uint32_t *h) {
  VkSurfaceCapabilitiesKHR caps = {};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, dev->surface, &caps);
  *w = caps.currentExtent.width;
  *h = caps.currentExtent.height;
  sc->format = VK_FORMAT_B8G8R8A8_SRGB;
  sc->extent.width = *w;
  sc->extent.height = *h;

  if (!create_vulkan_swapchain(dev, sc, VK_NULL_HANDLE))
    return false;

  // --- INITIAL SETUP (New Handles) ---
  uint32_t image_count = 0;
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, NULL);
  VkImage swap_images[image_count];
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, swap_images);

  vec_init_with_capacity(&sc->imgs, image_count, sizeof(PresentFrame), NULL);

  for (uint32_t i = 0; i < image_count; i++) {
    PresentFrame present = {};

    // Create View
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = swap_images[i],
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = sc->format,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    VkImageView view = {};
    vkCreateImageView(dev->device, &vi, NULL, &view);

    // Create Semaphore
    VkSemaphoreCreateInfo info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCreateSemaphore(dev->device, &info, NULL, &present.sem_rend_done);

    // Import NEW Resource
    RGImageInfo image_info = {.name = "SwapchainImage",
                              .format = sc->format,
                              .width = sc->extent.width,
                              .height = sc->extent.height,
                              .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};

    present.handle = rm_import_image(rm, &image_info, swap_images[i], view);

    vec_push(&sc->imgs, &present);
  }

  sc->current_img_idx = 0;
  return true;
}

void swapchain_resize(GPUDevice *dev, ResourceManager *rm, GPUSwapchain *sc, VkExtent2D *extent) {

  VkSurfaceCapabilitiesKHR caps = {};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, dev->surface, &caps);

  VkSwapchainKHR old_sc = sc->swapchain;
  sc->extent.width = extent->width;
  sc->extent.height = extent->height;

  // Create new using old as reference
  if (!create_vulkan_swapchain(dev, sc, old_sc)) {
    // Handle error (panic?)
    return;
  }

  // Destroy old swapchain wrapper
  vkDestroySwapchainKHR(dev->device, old_sc, NULL);

  // 4. Update Images
  uint32_t image_count = 0;
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, NULL);
  VkImage swap_images[image_count];
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, swap_images);

  if (image_count != sc->imgs.length) {
    LOG_ERROR("[SWAPCHAIN RESIZE]: after resize differnt image count for swapchain");
    abort();
  }

  for (uint32_t i = 0; i < image_count; i++) {
    PresentFrame *frame = VEC_AT(&sc->imgs, i, PresentFrame);

    // A. Recreate View
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                .image = swap_images[i],
                                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = sc->format,
                                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkImageView view = {};
    vkCreateImageView(dev->device, &vi, NULL, &view);

    // C. UPDATE EXISTING RESOURCE HANDLE
    RGImageInfo image_info = {.name = "SwapchainImage",
                              .format = sc->format,
                              .width = sc->extent.width,
                              .height = sc->extent.height,
                              .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};

    rm_import_existing_image(rm, frame->handle, swap_images[i], view, extent, false);
  }
  sc->current_img_idx = 0;
}

ResHandle swapchain_get_image(GPUSwapchain *sc) { return VEC_AT(&sc->imgs, sc->current_img_idx, PresentFrame)->handle; }

void swapchain_destroy(GPUDevice *dev, GPUSwapchain *sc) {}

// --- Private Functions ---

// Helper to create the actual VkSwapchainKHR object (avoids code duplication)
static bool create_vulkan_swapchain(GPUDevice *dev, GPUSwapchain *sc, VkSwapchainKHR old_swapchain) {
  VkSwapchainCreateInfoKHR ci = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = dev->surface,
      .minImageCount = 3,
      .imageFormat = sc->format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = sc->extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
      .oldSwapchain = old_swapchain // Handle the old one if provided
  };

  return vkCreateSwapchainKHR(dev->device, &ci, NULL, &sc->swapchain) == VK_SUCCESS;
}
