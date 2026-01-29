#include "submit_manager.h"
#include "gpu/gpu.h"
#include "util.h"
#include <stdlib.h>

typedef struct M_Submit {
  VkDevice device;
  VkQueue queue;

  u32 frames_in_flight;
  u32 current_frame;

  // Synchronization
  VkSemaphore timeline_sem;

  // THE LIST: Maps "Frame Slot" -> "Timeline Value to wait for"
  // frame_fences[0] might store 100
  // frame_fences[1] might store 101
  
  bool is_present_enabled;
  u64 frame_done_signal[MAX_FRAMES_IN_FLIGHT];
} M_Submit;

// --- Private Prototypes ---
static void _destroy(void *s);

// --- API ---

void sm_begin_frame(M_Submit *mgr) {
  // Look up what Index this frame slot is waiting for
  u64 wait_val = mgr->frame_done_signal[mgr->current_frame];

  u64 gpu_val = sm_get_gpu_ticket(mgr);
  // If we haven't reached that value yet, wait!
  if (wait_val > gpu_val) {
    VkSemaphoreWaitInfo waitInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                    .semaphoreCount = 1,
                                    .pSemaphores = &mgr->timeline_sem,
                                    .pValues = &wait_val};
    // 10 second timeout usually indicates a deadlock/crash
    vkWaitSemaphores(mgr->device, &waitInfo, 10000000000);

  }

  mgr->frame_done_signal[mgr->current_frame] =   mgr->frame_done_signal[mgr->current_frame] + 1;


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

u64 sm_work(M_Submit *mgr, M_Swapchain *swapchains, u32 swapchain_count, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit) {
  VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cmd_info};

  VkSemaphoreSubmitInfo wait_infos[2]; // Reduced array size
  VkSemaphoreSubmitInfo signal_infos[3];
  submit.pWaitSemaphoreInfos = wait_infos;
  submit.pSignalSemaphoreInfos = signal_infos;

  for(u32 i = 0; i < swapchain_count; i++)
  {
  // Wait on Swapchain Acquire (Binary)
  if (swapchains && is_first_submit) {
    wait_infos[submit.waitSemaphoreInfoCount++] =
        (VkSemaphoreSubmitInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                .semaphore = swapchains[i].sem_acquire[mgr->current_frame],
                                .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  }

  // Signal Render Finished (Binary for Present)
  if (swapchains && is_last_in_frame) {
    signal_infos[submit.signalSemaphoreInfoCount++] =
        (VkSemaphoreSubmitInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                .semaphore = swapchains[i].sem_render_finished[swapchains[i].current_img_idx],
                                .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
  }
}

  mgr->frame_done_signal[mgr->current_frame] += 1;
  
  signal_infos[submit.signalSemaphoreInfoCount++] = (VkSemaphoreSubmitInfo){
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore = mgr->timeline_sem,
    .value =  mgr->frame_done_signal[mgr->current_frame],
    .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  };

  vkQueueSubmit2(mgr->queue, 1, &submit, VK_NULL_HANDLE);

  return   mgr->frame_done_signal[mgr->current_frame];
}
void sm_on_frame_end(M_Submit *mgr){
  
  u32 old_frame_idx = mgr->current_frame;
  mgr->current_frame = (mgr->current_frame + 1) % mgr->frames_in_flight;


    return;
}
void sm_present(M_Submit *mgr, M_Swapchain *swapchain) {
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
}

// --- Helpers ---
u64 sm_get_cpu_ticket(M_Submit *mgr) { return   mgr->frame_done_signal[mgr->current_frame]; }

u64 sm_get_gpu_ticket(M_Submit *mgr) {
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

M_Submit *sm_init(VkDevice dev, VkQueue queue) {
  M_Submit *mgr = calloc(sizeof(M_Submit), 1);

  mgr->device = dev;
  mgr->queue = queue;
  mgr->frames_in_flight = MAX_FRAMES_IN_FLIGHT;
  mgr->is_present_enabled = true;
  mgr->timeline_sem =  vk_create_semp_timeline(mgr->device, "Semaphore-Timeline");

  return mgr;
}
