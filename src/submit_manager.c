#include "submit_manager.h"
#include "gpu/gpu.h"
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

typedef struct M_Submit {
  VkDevice device;
  VkQueue queue;

  u32 frames_in_flight;
  u32 current_frame;

  // Synchronization
  VkSemaphore timeline_sem;
  u64 timeline_value; // The counter we are currently building (e.g., 101)

  // THE LIST: Maps "Frame Slot" -> "Timeline Value to wait for"
  // frame_fences[0] might store 100
  // frame_fences[1] might store 101
  u64 *frame_done_signal;

  bool is_present_enabled;
} M_Submit;

// --- Private Prototypes ---
static void _destroy(void *s);



// --- API ---

void sm_begin_frame(M_Submit *mgr) {
  // Look up what Index this frame slot is waiting for
  u64 wait_val = mgr->frame_done_signal[mgr->current_frame];

  // If we haven't reached that value yet, wait!
  if (wait_val > 0) {
    VkSemaphoreWaitInfo waitInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                    .semaphoreCount = 1,
                                    .pSemaphores = &mgr->timeline_sem,
                                    .pValues = &wait_val};
    // 10 second timeout usually indicates a deadlock/crash
    vkWaitSemaphores(mgr->device, &waitInfo, 10000000000);
  }

  // Increment global counter for the NEW frame we are about to start
  mgr->timeline_value++;

  // Update the list: "This slot is now busy until 'timeline_value' is reached"
  mgr->frame_done_signal[mgr->current_frame] = mgr->timeline_value;
}

void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain) {
  if (!swapchain)
    return;
  VkAcquireNextImageInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain->swapchain,
      .timeout = UINT64_MAX,
      .semaphore = swapchain->sem_acquire[mgr->current_frame],
      .deviceMask = 1,
  };
  vkAcquireNextImage2KHR(mgr->device, &info, &swapchain->current_img_idx);
}
u64 sm_work(M_Submit *mgr, M_Swapchain *swapchain, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit) {
  VkCommandBufferSubmitInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd
  };
  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, 
      .commandBufferInfoCount = 1, 
      .pCommandBufferInfos = &cmd_info
  };

  VkSemaphoreSubmitInfo wait_infos[1];   // Reduced array size
  VkSemaphoreSubmitInfo signal_infos[2];
  submit.pWaitSemaphoreInfos = wait_infos;
  submit.pSignalSemaphoreInfos = signal_infos;

  // Wait on Swapchain Acquire (Binary)
  if (swapchain && is_first_submit) {
    wait_infos[submit.waitSemaphoreInfoCount++] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = swapchain->sem_acquire[mgr->current_frame],
        .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };
  }

  // Signal Render Finished (Binary for Present)
  if (swapchain && is_last_in_frame) {
    signal_infos[submit.signalSemaphoreInfoCount++] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = swapchain->sem_render_finished[swapchain->current_img_idx],
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    };
  }

  // 3. Signal Timeline (Host Synchronization)
  // This tells the CPU "Frame N is done".
  if (is_last_in_frame) {
    signal_infos[submit.signalSemaphoreInfoCount++] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = mgr->timeline_sem,
        .value = mgr->timeline_value,
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    };
  }

  vkQueueSubmit2(mgr->queue, 1, &submit, VK_NULL_HANDLE);

  return mgr->timeline_value;
}

void sm_present(M_Submit *mgr, M_Swapchain *swapchain, bool advance_frame) {
  if (!mgr->is_present_enabled || !swapchain) {
    if (advance_frame)
      mgr->current_frame = (mgr->current_frame + 1) % mgr->frames_in_flight;
    return;
  }
  VkSemaphore wait_sem = swapchain->sem_render_finished[swapchain->current_img_idx];
  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &wait_sem,
      .swapchainCount = 1,
      .pSwapchains = &swapchain->swapchain,
      .pImageIndices = &swapchain->current_img_idx,
  };
  vkQueuePresentKHR(mgr->queue, &present_info);

  if (advance_frame) {
    mgr->current_frame = (mgr->current_frame + 1) % mgr->frames_in_flight;
  }
}

// --- Helpers ---
u64 sm_get_cpu_ticket(M_Submit *mgr){
  return mgr->timeline_value;
}

u64 sm_get_gpu_ticket(M_Submit *mgr){
  u64 gpu_ticket = 0;
  vkGetSemaphoreCounterValue(mgr->device, mgr->timeline_sem, &gpu_ticket);
  return gpu_ticket;
}
bool sm_is_done(M_Submit *mgr, u64 timeline_index) {
  u64 current_val;
  vkGetSemaphoreCounterValue(mgr->device, mgr->timeline_sem, &current_val);
  return current_val >= timeline_index;
}

void sm_wait(M_Submit *mgr, u64 timeline_index) {
  VkSemaphoreWaitInfo waitInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                  .semaphoreCount = 1,
                                  .pSemaphores = &mgr->timeline_sem,
                                  .pValues = &timeline_index};
  vkWaitSemaphores(mgr->device, &waitInfo, UINT64_MAX);
}
// --- Private Functions ---

static void _destroy(void *s) {
  M_Submit *mgr = (M_Submit *)s;
  vkDeviceWaitIdle(mgr->device);
  vkDestroySemaphore(mgr->device, mgr->timeline_sem, NULL);
  free(mgr->frame_done_signal);
}

 M_Submit* sm_init(VkDevice dev, VkQueue queue) {
  M_Submit *mgr = calloc(sizeof(M_Submit), 1);

  mgr->device = dev;
  mgr->queue = queue;
  mgr->frames_in_flight = MAX_FRAMES_IN_FLIGHT;
  mgr->current_frame = 0;
  mgr->timeline_value = 0; // Start at 0
  mgr->is_present_enabled = true;

  // Allocate the list
  mgr->frame_done_signal = calloc(mgr->frames_in_flight, sizeof(u64));

  // Timeline Semaphore
  VkSemaphoreTypeCreateInfo timelineCreateInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                  .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                                  .initialValue = 0};
  VkSemaphoreCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineCreateInfo};
  vkCreateSemaphore(mgr->device, &createInfo, NULL, &mgr->timeline_sem);

  return mgr;
}
