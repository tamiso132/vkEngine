#include "gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system_manager.h"

#include "util.h"
#include "vector.h"
#include "vk_mem_alloc.h"

typedef struct QueueFamilyIndices {
  int graphics;
  int present;
  int transfer;
} QueueFamilyIndices;

static QueueFamilyIndices _find_queue_families(VkPhysicalDevice phys, VkSurfaceKHR surface) {
  QueueFamilyIndices out = {.graphics = -1, .present = -1, .transfer = -1};

  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, NULL);

  VkQueueFamilyProperties *props = malloc(sizeof(VkQueueFamilyProperties) * count);
  vkGetPhysicalDeviceQueueFamilyProperties(phys, &count, props);

  // 1) Graphics family
  for (uint32_t i = 0; i < count; i++) {
    if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      out.graphics = (int)i;
      break;
    }
  }

  // 2) Present family (requires surface)
  if (surface != VK_NULL_HANDLE) {
    for (uint32_t i = 0; i < count; i++) {
      VkBool32 supportsPresent = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &supportsPresent);
      if (supportsPresent) {
        out.present = (int)i;
        break;
      }
    }
  } else {
    out.present = out.graphics;
  }

  // 3) Transfer family: prefer transfer-only (no graphics + no compute)
  for (uint32_t i = 0; i < count; i++) {
    VkQueueFlags f = props[i].queueFlags;
    bool hasTransfer = (f & VK_QUEUE_TRANSFER_BIT) != 0;
    bool hasGraphics = (f & VK_QUEUE_GRAPHICS_BIT) != 0;
    bool hasCompute = (f & VK_QUEUE_COMPUTE_BIT) != 0;

    if (hasTransfer && !hasGraphics && !hasCompute) {
      out.transfer = (int)i;
      break;
    }
  }

  // fallback: any queue supporting transfer
  if (out.transfer < 0) {
    for (uint32_t i = 0; i < count; i++) {
      if (props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
        out.transfer = (int)i;
        break;
      }
    }
  }

  // final fallback
  if (out.transfer < 0)
    out.transfer = out.graphics;
  if (out.present < 0)
    out.present = out.graphics;

  free(props);
  return out;
}

// --- Private Prototypes ---
static bool on_system_init(void *config, u32 *memory_req);

static void on_system_destroy();
static bool gpu_init(M_GPU *dev, GLFWwindow *window, GPUInstanceInfo *info);
static void gpu_destroy(M_GPU *dev);
static int _rate_device(VkPhysicalDevice dev);

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                     VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                     void *pUserData) {
  // Filter out "VERBOSE" if you want less noise
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    fprintf(stderr, "[VALIDATION]: %s\n", pCallbackData->pMessage);
  }

  return VK_FALSE;
}

SystemFunc gpu_system_get_func() { return (SystemFunc){.on_init = on_system_init, .on_shutdown = on_system_destroy}; }

// --- Private Functions ---

static bool on_system_init(void *config, u32 *mem_req) {

  SYSTEM_HELPER_MEM(mem_req, M_GPU);

  GPUSystemInfo *c = config;
  M_GPU *device = m_system_get(SYSTEM_TYPE_GPU);

  return gpu_init(device, c->window, &c->info);
}

static void on_system_destroy() {
  M_GPU *device = m_system_get(SYSTEM_TYPE_GPU);
  gpu_destroy(device);
}

static bool gpu_init(M_GPU *dev, GLFWwindow *window, GPUInstanceInfo *info) {
  memset(dev, 0, sizeof(M_GPU));

  // 1. Volk & Instance
  if (volkInitialize() != VK_SUCCESS)
    return false;

  uint32_t glfwCount;
  const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
  const char **enabledExts = malloc(sizeof(char *) * (glfwCount + 1));
  DEFER_FREE_PTR(enabledExts);

  memcpy(enabledExts, glfwExts, sizeof(char *) * glfwCount);
  uint32_t extCount = glfwCount;

  if (info->enable_validation) {
    enabledExts[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  }
  // Layers
  const char *layers[] = {"VK_LAYER_KHRONOS_validation"};

  VkDebugUtilsMessengerCreateInfoEXT debugInfo = {.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
                                                  .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                                  .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                                                  .pfnUserCallback = debug_callback};

  VkInstanceCreateInfo instInfo = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                   .pNext = info->enable_validation ? &debugInfo : NULL,
                                   .enabledExtensionCount = extCount,
                                   .ppEnabledExtensionNames = enabledExts,
                                   .enabledLayerCount = info->enable_validation ? 1 : 0,
                                   .ppEnabledLayerNames = layers,
                                   .pApplicationInfo = &(VkApplicationInfo){.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                                                            .apiVersion = VK_API_VERSION_1_3}};

  if (vkCreateInstance(&instInfo, NULL, &dev->instance) != VK_SUCCESS)
    return false;
  volkLoadInstance(dev->instance);

  if (info->enable_validation) {
    vkCreateDebugUtilsMessengerEXT(dev->instance, &debugInfo, NULL, &dev->debug_messenger);
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
  // 4. Logical Device (Graphics + Present + Transfer)
  QueueFamilyIndices q = _find_queue_families(dev->physical_device, dev->surface);

  dev->graphics_family = (uint32_t)q.graphics;
  dev->present_family = (uint32_t)q.present;
  dev->transfer_family = (uint32_t)q.transfer;

  // One VkDeviceQueueCreateInfo per UNIQUE family
  VkDeviceQueueCreateInfo qInfos[3];
  uint32_t qInfoCount = 0;

#define ADD_QFI(familyIndex)                                                                                           \
  do {                                                                                                                 \
    bool exists = false;                                                                                               \
    for (uint32_t k = 0; k < qInfoCount; k++) {                                                                        \
      if (qInfos[k].queueFamilyIndex == (uint32_t)(familyIndex)) {                                                     \
        exists = true;                                                                                                 \
        break;                                                                                                         \
      }                                                                                                                \
    }                                                                                                                  \
    if (!exists) {                                                                                                     \
      qInfos[qInfoCount++] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,            \
                                                       .queueFamilyIndex = (uint32_t)(familyIndex),                    \
                                                       .queueCount = 1,                                                \
                                                       .pQueuePriorities = &prio};                                     \
    }                                                                                                                  \
  } while (0)

  ADD_QFI(dev->graphics_family);
  ADD_QFI(dev->present_family);
  ADD_QFI(dev->transfer_family);
#undef ADD_QFI

  printf("[GPU] queue families: graphics=%u present=%u transfer=%u\n", dev->graphics_family, dev->present_family,
         dev->transfer_family);

  VkPhysicalDeviceDescriptorIndexingFeatures indexing_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE, // textures[non_const_idx]
      .runtimeDescriptorArray = VK_TRUE,                    // textures[] (obestämd storlek)
      .descriptorBindingPartiallyBound = VK_TRUE,           // Inte alla slots måste vara fyllda
      .descriptorBindingVariableDescriptorCount = VK_TRUE,
      .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
      .descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE,
      .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
      // Uppdatera set medan det används
  };

  // Kedja ihop features: Features2 -> DynamicRendering -> Sync2 -> Indexing
  VkPhysicalDeviceSynchronization2Features sync2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
      .synchronization2 = VK_TRUE,
      .pNext = &indexing_features // <--- Kedja här
  };

  VkPhysicalDeviceDynamicRenderingFeatures dynR = {.sType =
                                                       VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
                                                   .dynamicRendering = VK_TRUE,
                                                   .pNext = &sync2};

  VkPhysicalDeviceTimelineSemaphoreFeatures timeline = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
      .timelineSemaphore = VK_TRUE,
      .pNext = &dynR};

  VkPhysicalDeviceFeatures2 feats2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                      .features.shaderInt64 = VK_TRUE, // Bra att ha
                                      .pNext = &timeline};
  const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo dInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .pNext = &feats2,
                              .queueCreateInfoCount = qInfoCount,
                              .pQueueCreateInfos = qInfos,
                              .enabledExtensionCount = 1,
                              .ppEnabledExtensionNames = devExts};

  if (vkCreateDevice(dev->physical_device, &dInfo, NULL, &dev->device) != VK_SUCCESS)
    return false;
  volkLoadDevice(dev->device);

 vkGetDeviceQueue(dev->device, dev->graphics_family, 0, &dev->graphics_queue);
vkGetDeviceQueue(dev->device, dev->transfer_family, 0, &dev->transfer_queue);


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
  VkCommandPoolCreateInfo poolInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                      .queueFamilyIndex = 0,
                                      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};

  vk_check(vkCreateCommandPool(dev->device, &poolInfo, NULL, &dev->imm_cmd_pool));

  VkCommandBufferAllocateInfo cmdInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                         .commandPool = dev->imm_cmd_pool,
                                         .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                         .commandBufferCount = 1};
  vkAllocateCommandBuffers(dev->device, &cmdInfo, &dev->imm_cmd_buffer);
  VkFenceCreateInfo fInfo = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; // Not signaled initially

  vk_check(vkCreateFence(dev->device, &fInfo, NULL, &dev->imm_fence));

  return true;
}

static void gpu_destroy(M_GPU *dev) {
  vkDeviceWaitIdle(dev->device);
  vmaDestroyAllocator(dev->allocator);
  vkDestroyCommandPool(dev->device, dev->imm_cmd_pool, NULL);
  vkDestroyFence(dev->device, dev->imm_fence, NULL);
  vkDestroyDevice(dev->device, NULL);
  vkDestroySurfaceKHR(dev->instance, dev->surface, NULL);
  vkDestroyInstance(dev->instance, NULL);
}

static int _rate_device(VkPhysicalDevice dev) {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(dev, &props);
  int score = 0;
  if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    score += 1000;
  score += props.limits.maxImageDimension2D;
  return score;
}
