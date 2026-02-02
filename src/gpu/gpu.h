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
typedef struct M_GPU {
  VkInstance instance;
  VkDevice device;
  VkPhysicalDevice physical_device;
  VkQueue graphics_queue;
  VkQueue compute_queue;
  VkQueue transfer_queue;
  uint32_t graphics_family;
  u32 present_family;
  u32 transfer_family;

  VkDebugUtilsMessengerEXT debug_messenger;

  VmaAllocator allocator;

  // Internal command pool for immediate submits
  VkCommandPool imm_cmd_pool;
  VkCommandBuffer imm_cmd_buffer;
  VkFence imm_fence;
} M_GPU;

typedef struct GPUSystemInfo {
  GPUInstanceInfo info;
} GPUSystemInfo;

// PUBLIC FUNCTIONS

SystemFunc gpu_system_get_func();
SystemFunc gpu_system_func();
