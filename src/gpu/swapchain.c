#include "swapchain.h"

bool swapchain_init(GPUDevice *dev, ResourceManager *rm, GPUSwapchain *sc,
                    uint32_t w, uint32_t h) {
  sc->format = VK_FORMAT_B8G8R8A8_SRGB; // Force for simplicity
  sc->extent.width = w;
  sc->extent.height = h;

  VkSwapchainCreateInfoKHR ci = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = dev->surface,
      .minImageCount = 3,
      .imageFormat = sc->format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = sc->extent,
      .imageArrayLayers = 1,
      .imageUsage =
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE};
  if (vkCreateSwapchainKHR(dev->device, &ci, NULL, &sc->swapchain) !=
      VK_SUCCESS)
    return false;

  // Fetch images
  u32 image_count = 0;
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, NULL);

  VkImage swap_images[image_count];
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count,
                          swap_images);

  vec_init_with_capacity(&sc->imgs, image_count, sizeof(PresentFrame), NULL);
  for (uint32_t i = 0; i < image_count; i++) {
    PresentFrame present = {};
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swap_images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = sc->format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};

    VkImageView view = {};
    vkCreateImageView(dev->device, &vi, NULL, &view);

    RGImageInfo image_info = {.name = "SwapchainImage",
                              .format = sc->format,
                              .height = sc->extent.height,
                              .width = sc->extent.width,
                              .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};

    VkSemaphoreCreateInfo info = {.sType =
                                      VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    present.handle = rm_import_image(rm, &image_info, swap_images[i], view);
    vkCreateSemaphore(dev->device, &info, NULL, &present.sem_rend_done);

    vec_push(&sc->imgs, &present);
  }

  return true;
}

ResHandle swapchain_get_image(GPUSwapchain *sc) {
  return VEC_AT(&sc->imgs, sc->current_img_idx, PresentFrame)->handle;
}

void swapchain_destroy(GPUDevice *dev, GPUSwapchain *sc) {}
