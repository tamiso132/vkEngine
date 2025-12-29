#include "gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk_mem_alloc.h"

// --- Private Prototypes ---
static int _rate_device(VkPhysicalDevice dev);

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageType,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void *pUserData) {
  // Filter out "VERBOSE" if you want less noise
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    fprintf(stderr, "[VALIDATION]: %s\n", pCallbackData->pMessage);
  }
  return VK_FALSE;
}

bool gpu_init(GPUDevice *dev, GLFWwindow *window, GPUInstanceInfo *info) {
  memset(dev, 0, sizeof(GPUDevice));

  // 1. Volk & Instance
  if (volkInitialize() != VK_SUCCESS)
    return false;

  uint32_t glfwCount;
  const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
  const char **enabledExts = malloc(sizeof(char *) * (glfwCount + 1));

  memcpy(enabledExts, glfwExts, sizeof(char *) * glfwCount);
  uint32_t extCount = glfwCount;

  if (info->enable_validation) {
    enabledExts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  }
  // Layers
  const char *layers[] = {"VK_LAYER_KHRONOS_validation"};

  VkDebugUtilsMessengerCreateInfoEXT debugInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debug_callback};

  VkInstanceCreateInfo instInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = info->enable_validation ? &debugInfo : NULL,
      .enabledExtensionCount = glfwCount + 1,
      .ppEnabledExtensionNames = enabledExts,
      .enabledLayerCount = info->enable_validation ? 1 : 0,
      .ppEnabledLayerNames = layers,
      .pApplicationInfo =
          &(VkApplicationInfo){.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .apiVersion = VK_API_VERSION_1_3}};
  if (vkCreateInstance(&instInfo, NULL, &dev->instance) != VK_SUCCESS)
    return false;
  volkLoadInstance(dev->instance);

  if (info->enable_validation) {
    vkCreateDebugUtilsMessengerEXT(dev->instance, &debugInfo, NULL,
                                   &dev->debug_messenger);
  }
  // 2. Surface
  if (window)
    glfwCreateWindowSurface(dev->instance, window, NULL, &dev->surface);

  // 3. Pick Physical Device
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(dev->instance, &count, NULL);
  VkPhysicalDevice *pdevs = malloc(count * sizeof(VkPhysicalDevice));
  vkEnumeratePhysicalDevices(dev->instance, &count, pdevs);

  int bestScore = -1;
  for (uint32_t i = 0; i < count; i++) {
    int score = _rate_device(pdevs[i]);
    if (score > bestScore) {
      dev->physical_device = pdevs[i];
      bestScore = score;
    }
  }
  free(pdevs);

  // 4. Logical Device (Simplified: 1 Queue for everything)
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qInfo = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex =
          0, // In robust code, find family with Graphics | Present
      .queueCount = 1,
      .pQueuePriorities = &prio};

  VkPhysicalDeviceDescriptorIndexingFeatures indexing_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
      .shaderSampledImageArrayNonUniformIndexing =
          VK_TRUE,                       // textures[non_const_idx]
      .runtimeDescriptorArray = VK_TRUE, // textures[] (obestämd storlek)
      .descriptorBindingPartiallyBound =
          VK_TRUE, // Inte alla slots måste vara fyllda
      .descriptorBindingVariableDescriptorCount = VK_TRUE,
      .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
      .descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
      // Uppdatera set medan det används
  };

  // Kedja ihop features: Features2 -> DynamicRendering -> Sync2 -> Indexing
  VkPhysicalDeviceSynchronization2Features sync2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
      .synchronization2 = VK_TRUE,
      .pNext = &indexing_features // <--- Kedja här
  };

  VkPhysicalDeviceDynamicRenderingFeatures dynR = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
      .dynamicRendering = VK_TRUE,
      .pNext = &sync2};

  VkPhysicalDeviceFeatures2 feats2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .features.shaderInt64 = VK_TRUE, // Bra att ha
      .pNext = &dynR};
  const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .pNext = &feats2,
                              .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qInfo,
                              .enabledExtensionCount = 1,
                              .ppEnabledExtensionNames = devExts};

  if (vkCreateDevice(dev->physical_device, &dInfo, NULL, &dev->device) !=
      VK_SUCCESS)
    return false;
  volkLoadDevice(dev->device);

  vkGetDeviceQueue(dev->device, 0, 0, &dev->graphics_queue);

  // 5. VMA Init
  VmaVulkanFunctions vmaFuncs = {0};
  vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo vmaInfo = {.physicalDevice = dev->physical_device,
                                    .device = dev->device,
                                    .instance = dev->instance,
                                    .vulkanApiVersion = VK_API_VERSION_1_3,
                                    .pVulkanFunctions = &vmaFuncs};
  vk_check(vmaCreateAllocator(&vmaInfo, &dev->allocator));

  // 6. Immediate Submit Context
  VkCommandPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = 0,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};

  vk_check(
      vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->imm_cmd_pool));

  VkCommandBufferAllocateInfo cmdInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = dev->imm_cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  vkAllocateCommandBuffers(dev->device, &cmdInfo, &dev->imm_cmd_buffer);
  VkFenceCreateInfo fInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; // Not signaled initially

  vk_check(vkCreateFence(dev->device, &fInfo, NULL, &dev->imm_fence));

  return true;
}

void gpu_immediate_submit(GPUDevice *dev,
                          void (*callback)(VkCommandBuffer, void *),
                          void *user_data) {
  vkResetFences(dev->device, 1, &dev->imm_fence);
  vkResetCommandBuffer(dev->imm_cmd_buffer, 0);

  VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  vkBeginCommandBuffer(dev->imm_cmd_buffer, &bi);

  callback(dev->imm_cmd_buffer, user_data);

  vkEndCommandBuffer(dev->imm_cmd_buffer);

  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                     .commandBufferCount = 1,
                     .pCommandBuffers = &dev->imm_cmd_buffer};
  vkQueueSubmit(dev->graphics_queue, 1, &si, dev->imm_fence);
  vkWaitForFences(dev->device, 1, &dev->imm_fence, VK_TRUE, UINT64_MAX);
}

// --- Swapchain Simplified ---
bool gpu_swapchain_init(GPUDevice *dev, GPUSwapchain *sc, uint32_t w,
                        uint32_t h) {
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
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &sc->image_count, NULL);
  sc->images = malloc(sc->image_count * sizeof(VkImage));
  sc->views = malloc(sc->image_count * sizeof(VkImageView));
  vkGetSwapchainImagesKHR(dev->device, sc->swapchain, &sc->image_count,
                          sc->images);

  for (uint32_t i = 0; i < sc->image_count; i++) {
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = sc->images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = sc->format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCreateImageView(dev->device, &vi, NULL, &sc->views[i]);
  }

  // Sync
  VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                          .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  vkCreateSemaphore(dev->device, &si, NULL, &sc->acquire_sem);
  vkCreateSemaphore(dev->device, &si, NULL, &sc->present_sem);
  vkCreateFence(dev->device, &fi, NULL, &sc->frame_fence);

  return true;
}

bool gpu_swapchain_acquire(GPUDevice *dev, GPUSwapchain *sc) {
  vkWaitForFences(dev->device, 1, &sc->frame_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(dev->device, 1, &sc->frame_fence);
  return vkAcquireNextImageKHR(dev->device, sc->swapchain, UINT64_MAX,
                               sc->acquire_sem, VK_NULL_HANDLE,
                               &sc->current_img_idx) == VK_SUCCESS;
}

void gpu_swapchain_present(GPUDevice *dev, GPUSwapchain *sc, VkQueue queue) {
  VkPresentInfoKHR pi = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores =
          &sc->present_sem, // Must match what the RenderGraph signaled
      .swapchainCount = 1,
      .pSwapchains = &sc->swapchain,
      .pImageIndices = &sc->current_img_idx};
  vkQueuePresentKHR(queue, &pi);
}

void gpu_swapchain_destroy(GPUDevice *dev, GPUSwapchain *sc) {}

void gpu_destroy(GPUDevice *dev) {
  vkDeviceWaitIdle(dev->device);
  vmaDestroyAllocator(dev->allocator);
  vkDestroyCommandPool(dev->device, dev->imm_cmd_pool, NULL);
  vkDestroyFence(dev->device, dev->imm_fence, NULL);
  vkDestroyDevice(dev->device, NULL);
  vkDestroySurfaceKHR(dev->instance, dev->surface, NULL);
  vkDestroyInstance(dev->instance, NULL);
}

// --- Private Functions ---

static int _rate_device(VkPhysicalDevice dev) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dev, &props);
  int score = 0;
  if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    score += 1000;
  score += props.limits.maxImageDimension2D;
  return score;
}
