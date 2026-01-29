#include "gpu/swapchain.h"
#include "gpu/gpu.h"
#include "resource/resmanager.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>

// --- Private Prototypes ---
void swapchain_init(M_Swapchain *sc, VkSurfaceKHR surface, const char *name) {
  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  M_Resource *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);

  // 1. Check Capabilities
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, surface, &caps);

  VkSurfaceFormatKHR *formats;
  u32 format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, surface, &format_count, NULL);
  formats = malloc(sizeof(VkSurfaceFormatKHR) * format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, surface, &format_count, formats);

  // Select Format (prefer B8G8R8A8_UNORM / SRGB)
  VkSurfaceFormatKHR surface_format = formats[0];
  for (u32 i = 0; i < format_count; i++) {
    if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      surface_format = formats[i];
      break;
    }
  }
  free(formats);

  sc->format = surface_format.format;
  sc->extent = caps.currentExtent;

  // 2. Create Swapchain
  u32 image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
    image_count = caps.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                          .surface = surface,
                                          .minImageCount = image_count,
                                          .imageFormat = surface_format.format,
                                          .imageColorSpace = surface_format.colorSpace,
                                          .imageExtent = sc->extent,
                                          .imageArrayLayers = 1,
                                          .imageUsage =
                                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                          .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                          .preTransform = caps.currentTransform,
                                          .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                                          .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                          .clipped = VK_TRUE,
                                          .oldSwapchain = VK_NULL_HANDLE};

  vkCreateSwapchainKHR(dev->device, &create_info, NULL, &sc->swapchain);

  // 3. Get Images
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, NULL);
  VkImage *vk_images = calloc(sizeof(VkImage) * image_count, 1);
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &image_count, vk_images);

  sc->image_count = image_count;
  sc->images = malloc(sizeof(ResHandle) * image_count);

  for (u32 i = 0; i < image_count; i++) {
    char img_name[64];
    snprintf(img_name, 64, "%s_Image_%d", name, i);
    RGImageInfo img_info = {.name = img_name,
                            .format = sc->format,
                            .height = sc->extent.height,
                            .width = sc->extent.width,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    sc->images[i] = rm_import_image(rm, &img_info, vk_images[i], NULL);
  }
  free(vk_images);

  VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  // A. Acquire Semaphores (Per Frame)
  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    sc->sem_acquire[i] = vk_create_semp_binary(dev->device, "Semaphore-Aquire");
  }

  // B. Render Finished Semaphores (Per Image)
  sc->sem_render_finished = malloc(sizeof(VkSemaphore) * sc->image_count);
  for (u32 i = 0; i < sc->image_count; i++) {
    sc->sem_render_finished[i] = vk_create_semp_binary(dev->device, "Semaphore-RenderFinished");
  }
}

ResHandle swapchain_get_image(M_Swapchain *sc) { return sc->images[sc->current_img_idx]; }

void swapchain_destroy(M_Swapchain *sc) {
  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(dev->device, sc->sem_acquire[i], NULL);
  }

  for (u32 i = 0; i < sc->image_count; i++) {
    vkDestroySemaphore(dev->device, sc->sem_render_finished[i], NULL);
  }
  free(sc->sem_render_finished);

  vkDestroySwapchainKHR(dev->device, sc->swapchain, NULL);
  free(sc->images);
}
// --- Private Functions ---
