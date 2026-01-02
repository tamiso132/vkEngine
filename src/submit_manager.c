#include "submit_manager.h"
#include "filewatch.h"
#include "gpu/gpu.h"
#include "gpu/swapchain.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct M_Submit {
  VkDevice device;
  VkQueue queue;
  VkSemaphore timeline;
  VkSemaphore binary_acquire;

  uint64_t frame_index;
  uint32_t frames_in_flight; // Hur många frames GPU får ligga efter
} M_Submit;

// --- Private Prototypes ---
static void _system_destroy();
static void _init(M_Submit *mgr, VkDevice device, VkQueue queue, uint32_t frames_in_flight);
static bool _system_init(void *config, u32 *mem_req);

SystemFunc sm_system_get_func() { return (SystemFunc){.on_init = _system_init, .on_shutdown = _system_destroy}; }

// -----------------------------------------------------------------------------
// CPU SYNKRONISERING
// -----------------------------------------------------------------------------

void sm_begin_frame(M_Submit *mgr) {

  mgr->frame_index++;
  if (mgr->frame_index <= mgr->frames_in_flight) {
    return; // Bufferten är inte full än, bara kör.
  }

  uint64_t wait_value = mgr->frame_index - mgr->frames_in_flight;

  uint64_t current_val;
  vkGetSemaphoreCounterValue(mgr->device, mgr->timeline, &current_val);

  if (current_val < wait_value) {
    VkSemaphoreWaitInfo waitInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                    .semaphoreCount = 1,
                                    .pSemaphores = &mgr->timeline,
                                    .pValues = &wait_value};
    vkWaitSemaphores(mgr->device, &waitInfo, UINT64_MAX);
  }
  mgr->frame_index++;
}

void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain) {

  VkAcquireNextImageInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain->swapchain,
      .semaphore = mgr->binary_acquire,
      .timeout = UINT64_MAX - 1,
      .deviceMask = 1 << 0,
  };

  vkAcquireNextImage2KHR(mgr->device, &info, &swapchain->current_img_idx);
}

void sm_work(M_Submit *mgr, M_Swapchain *swapchain, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit) {

  VkCommandBufferSubmitInfo cmd_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};

  VkSemaphoreSubmitInfo wait_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                     .semaphore = mgr->binary_acquire,
                                     .stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};

  VkSemaphoreSubmitInfo signal_info[2] = {
      {
          .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
          .semaphore = mgr->timeline,
          .value = mgr->frame_index,
          .stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      },

      {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
       .semaphore = VEC_AT(&swapchain->imgs, swapchain->current_img_idx, PresentFrame)->sem_rend_done,
       .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};

  VkSubmitInfo2 submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                          .commandBufferInfoCount = 1,
                          .pCommandBufferInfos = &cmd_info,

                          // wait on aquire first submit
                          // TODO: optimize so only waits if it uses swapchain image
                          .waitSemaphoreInfoCount = is_first_submit ? 1 : 0,
                          .pWaitSemaphoreInfos = &wait_info,

                          // Vi signalerar ENDAST om det är sista biten av framen.
                          .signalSemaphoreInfoCount = is_last_in_frame ? 2 : 0,
                          .pSignalSemaphoreInfos = signal_info};

  if (is_last_in_frame) {
    // LOG_INFO("[QueueSubmit]: Signal at %ld", signal_info[0].value);
  }

  vkQueueSubmit2(mgr->queue, 1, &submit, VK_NULL_HANDLE);
}

void sm_present(M_Submit *mgr, M_Swapchain *swapchain) {

  VkSemaphore *sem_rend_done = &VEC_AT(&swapchain->imgs, swapchain->current_img_idx, PresentFrame)->sem_rend_done;

  VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = sem_rend_done,
      .swapchainCount = 1,
      .pSwapchains = &swapchain->swapchain,
      .pImageIndices = &swapchain->current_img_idx,
  };

  vkQueuePresentKHR(mgr->queue, &present_info);
}

// --- Private Functions ---

static void _system_destroy() {
  auto *mgr = SYSTEM_GET(SYSTEM_TYPE_SUBMIT, M_Submit);
  vkDestroySemaphore(mgr->device, mgr->timeline, NULL);
}

static void _init(M_Submit *mgr, VkDevice device, VkQueue queue, uint32_t frames_in_flight) {

  mgr->device = device;
  mgr->queue = queue;
  mgr->frames_in_flight = frames_in_flight;
  mgr->frame_index = 0; // Vi börjar räkna frames från 1 vid första submit

  // Skapa en enda Timeline Semaphore
  VkSemaphoreTypeCreateInfo typeInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                        .initialValue = 0};

  VkSemaphoreCreateInfo semInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &typeInfo};

  if (vkCreateSemaphore(device, &semInfo, NULL, &mgr->timeline) != VK_SUCCESS) {
    printf("[SubmitManager] Failed to create timeline semaphore\n");
    free(mgr);
    abort();
  }

  VkSemaphoreCreateInfo binary_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

  vkCreateSemaphore(device, &binary_info, NULL, &mgr->binary_acquire);
}

static bool _system_init(void *config, u32 *mem_req) {
  SYSTEM_HELPER_MEM(mem_req, M_Submit);

  auto *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  auto *mgr = SYSTEM_GET(SYSTEM_TYPE_SUBMIT, M_Submit);

  VkExtent2D extent = {};
  _init(mgr, dev->device, dev->graphics_queue, 1);
  return true;
}
