#include "submit_manager.h"
#include "gpu/gpu.h"
#include <stdlib.h>

#include "common.h"

typedef struct FrameData {
  VkFence in_flight;
  VkSemaphore sem_acquire; // "Image Available" (Per Frame In Flight)
} FrameData;

typedef struct M_Submit {
  VkDevice device;
  VkQueue graphics_queue;

  u32 frames_in_flight;
  u32 current_frame;
  FrameData *frames;

  bool is_present_enabled;
} M_Submit;

// --- Private Prototypes ---
static void _destroy(void *s);

static void _init(void *s);

// --- API ---

void sm_begin_frame(M_Submit *mgr) {
  // 1. Wait for the PREVIOUS usage of this Frame Slot to finish
  vkWaitForFences(mgr->device, 1, &mgr->frames[mgr->current_frame].in_flight, VK_TRUE, UINT64_MAX);
  vkResetFences(mgr->device, 1, &mgr->frames[mgr->current_frame].in_flight);
}

void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain) {
  if (!swapchain)
    return;
  FrameData *frame = &mgr->frames[mgr->current_frame];

  // 2. Acquire Next Image
  // We use the semaphore belonging to the current FRAME slot.
  VkAcquireNextImageInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain->swapchain,
      .timeout = UINT64_MAX,
      .semaphore = frame->sem_acquire, // Input: Signal this when image is ready
      .deviceMask = 1,
  };

  // Vulkan returns the index of the image we actually got.
  vkAcquireNextImage2KHR(mgr->device, &info, &swapchain->current_img_idx);
}

u64 sm_work(M_Submit *mgr, M_Swapchain *swapchain, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit) {
  FrameData *frame = &mgr->frames[mgr->current_frame];

  VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};

  // 3. Configure Submit
  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmd_info,
  };

  VkSemaphoreSubmitInfo wait_info = {0};
  VkSemaphoreSubmitInfo signal_info = {0};

  // A. WAIT: If this is the start of the frame, wait for the Image to be Acquired
  if (swapchain && is_first_submit) {
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame->sem_acquire; // Per-Frame Semaphore
    wait_info.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait_info;
  }

  // B. SIGNAL: If this is the end of the frame, signal that Rendering is Finished
  if (swapchain && is_last_in_frame) {
    // We signal the semaphore belonging to the SPECIFIC IMAGE we are drawing to.
    // This allows safe reuse as per the "Good Code" pattern.
    VkSemaphore signal_sem = swapchain->sem_render_finished[swapchain->current_img_idx];

    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = signal_sem; // Per-Image Semaphore
    signal_info.stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal_info;
  }

  // Signal the CPU Fence only on the last submission
  VkFence fence = (is_last_in_frame) ? frame->in_flight : VK_NULL_HANDLE;

  vkQueueSubmit2(mgr->graphics_queue, 1, &submit, fence);

  return 0;
}

void sm_present(M_Submit *mgr, M_Swapchain *swapchain) {
  if (!mgr->is_present_enabled || !swapchain) {
    mgr->current_frame = (mgr->current_frame + 1) % mgr->frames_in_flight;
    return;
  }

  // 4. Present
  // Wait for the per-image "Render Finished" semaphore
  VkSemaphore wait_sem = swapchain->sem_render_finished[swapchain->current_img_idx];

  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &wait_sem,
      .swapchainCount = 1,
      .pSwapchains = &swapchain->swapchain,
      .pImageIndices = &swapchain->current_img_idx,
  };

  vkQueuePresentKHR(mgr->graphics_queue, &present_info);

  // Advance Frame Slot
  mgr->current_frame = (mgr->current_frame + 1) % mgr->frames_in_flight;
}

// --- Private Functions ---

static void _destroy(void *s) {
  M_Submit *mgr = (M_Submit *)s;
  vkDeviceWaitIdle(mgr->device);

  for (u32 i = 0; i < mgr->frames_in_flight; i++) {
    vkDestroySemaphore(mgr->device, mgr->frames[i].sem_acquire, NULL);
    vkDestroyFence(mgr->device, mgr->frames[i].in_flight, NULL);
  }
  free(mgr->frames);
}

static void _init(void *s) {
  M_Submit *mgr = (M_Submit *)s;
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  mgr->device = gpu->device;
  mgr->graphics_queue = gpu->graphics_queue;
  mgr->frames_in_flight = 2; // Double Buffering
  mgr->current_frame = 0;
  mgr->is_present_enabled = true;

  mgr->frames = malloc(sizeof(FrameData) * mgr->frames_in_flight);

  VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT // Start signaled to skip wait on first frame
  };

  for (u32 i = 0; i < mgr->frames_in_flight; i++) {
    vkCreateSemaphore(mgr->device, &sem_info, NULL, &mgr->frames[i].sem_acquire);
    vkCreateFence(mgr->device, &fence_info, NULL, &mgr->frames[i].in_flight);
  }
}
