#pragma once
#define VK_NO_PROTOTYPES

#include "vector.h"

#include "util.h"
#include "vk_mem_alloc.h"
#include "volk.h"
#include <GLFW/glfw3.h>
#include <stdbool.h>

// --- Types ---
typedef struct GPUDevice GPUDevice;

// --- Config Structs (Builder Pattern) ---
typedef struct {
  const char *app_name;
  bool enable_validation;
} GPUInstanceInfo;

typedef struct {
  size_t size;
  VkBufferUsageFlags usage;
  VmaMemoryUsage memory_usage; // e.g., VMA_MEMORY_USAGE_AUTO
  const char *debug_name;
} GPUBufferInfo;

typedef struct {
  VkFormat format;
  VkExtent3D extent;
  VkImageUsageFlags usage;
  const char *debug_name;
} GPUImageInfo;

// --- The Main Device Context ---
struct GPUDevice {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice physical_device;
  VkQueue graphics_queue;
  VkQueue compute_queue;
  VkQueue transfer_queue;
  uint32_t graphics_family;

  VkDebugUtilsMessengerEXT debug_messenger;

  VmaAllocator allocator;
  VkSurfaceKHR surface; // Optional, usually tied to window

  // Internal command pool for immediate submits
  VkCommandPool imm_cmd_pool;
  VkCommandBuffer imm_cmd_buffer;
  VkFence imm_fence;
};

// PUBLIC FUNCTIONS

bool gpu_init(GPUDevice *dev, GLFWwindow *window, GPUInstanceInfo *info);

void gpu_destroy(GPUDevice *dev);
