#pragma once
#define VK_NO_PROTOTYPES

#include "vector.h"

#include "system_manager.h"
#include "util.h"
#include "vk_mem_alloc.h"
#include "volk.h"
#include <GLFW/glfw3.h>
#include <stdbool.h>

// --- Types ---

// --- Config Structs (Builder Pattern) ---
typedef struct {
  const char *app_name;
  bool enable_validation;
} GPUInstanceInfo;

// --- The Main Device Context ---
struct M_GPU {
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

typedef struct GPUSystemInfo {
  GLFWwindow *window;
  GPUInstanceInfo info;
} GPUSystemInfo;

// PUBLIC FUNCTIONS

SystemFunc gpu_system_get_func();
SystemFunc gpu_system_func();
