#ifndef GPU_H
#define GPU_H

#define VK_NO_PROTOTYPES

#include "vk_mem_alloc.h"
#include "volk.h"
#include <GLFW/glfw3.h>
#include <stdbool.h>

// --- Types ---
typedef struct GPUDevice GPUDevice;

// Handles (Opaque identifiers, DAXA style)
typedef struct {
  VkBuffer vk;
  VmaAllocation alloc;
} GPUBuffer;

typedef struct {
  VkImage vk;
  VkImageView view;
  VmaAllocation alloc;
} GPUImage;

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

// --- API Functions ---

// 1. Setup
// Creates Instance, Device, Allocator, and Default Queues.
// Uses "score" logic to pick the best discrete GPU automatically.
bool gpu_init(GPUDevice *device, GLFWwindow *window, GPUInstanceInfo *info);
void gpu_destroy(GPUDevice *device);

// 2. Resource Management (VMA Wrapper)
GPUBuffer gpu_create_buffer(GPUDevice *dev, GPUBufferInfo *info);
void gpu_destroy_buffer(GPUDevice *dev, GPUBuffer buffer);

GPUImage gpu_create_image(GPUDevice *dev, GPUImageInfo *info);
void gpu_destroy_image(GPUDevice *dev, GPUImage image);

// 3. Swapchain (Manages Resize/Sync)
typedef struct {
  VkSwapchainKHR swapchain;
  VkFormat format;
  VkExtent2D extent;
  VkImage *images;    // array
  VkImageView *views; // array
  uint32_t image_count;

  // Sync per frame
  VkSemaphore acquire_sem;
  VkSemaphore present_sem;
  VkFence frame_fence;
  uint32_t current_img_idx;
} GPUSwapchain;

bool gpu_swapchain_init(GPUDevice *dev, GPUSwapchain *sc, uint32_t w,
                        uint32_t h);
bool gpu_swapchain_acquire(GPUDevice *dev, GPUSwapchain *sc);
void gpu_swapchain_present(GPUDevice *dev, GPUSwapchain *sc,
                           VkQueue queue); // Uses semaphore from acquire
void gpu_swapchain_destroy(GPUDevice *dev, GPUSwapchain *sc);

// 4. Utility (One-shot commands)
void gpu_immediate_submit(GPUDevice *dev,
                          void (*callback)(VkCommandBuffer cmd, void *user),
                          void *user_data);

#endif