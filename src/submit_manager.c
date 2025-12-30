#include "submit_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------------
// DATA
// -----------------------------------------------------------------------------

typedef struct SubmitManager_T {
  VkDevice device;
  VkQueue queue;
  VkSemaphore timeline;

  uint64_t frame_index;
  uint32_t frames_in_flight; // Hur många frames GPU får ligga efter
} SubmitManager_T;

// --- Private Prototypes ---

SubmitManager submit_manager_create(VkDevice device, VkQueue queue,
                                    uint32_t frames_in_flight) {

  SubmitManager mgr = calloc(1, sizeof(SubmitManager_T));

  mgr->device = device;
  mgr->queue = queue;
  mgr->frames_in_flight = frames_in_flight;
  mgr->frame_index = 0; // Vi börjar räkna frames från 1 vid första submit

  // Skapa en enda Timeline Semaphore
  VkSemaphoreTypeCreateInfo typeInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = 0};

  VkSemaphoreCreateInfo semInfo = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &typeInfo};

  if (vkCreateSemaphore(device, &semInfo, NULL, &mgr->timeline) != VK_SUCCESS) {
    printf("[SubmitManager] Failed to create timeline semaphore\n");
    free(mgr);
    return NULL;
  }

  return mgr;
}
void submit_manager_destroy(SubmitManager mgr) {
  if (!mgr)
    return;
  vkDestroySemaphore(mgr->device, mgr->timeline, NULL);
  free(mgr);
}

// -----------------------------------------------------------------------------
// CPU SYNKRONISERING
// -----------------------------------------------------------------------------

void submit_begin_frame(SubmitManager mgr) {
  // Öka frame index. Frame 1, Frame 2, osv.
  // Detta blir vårt mål-värde för semaforen.
  mgr->frame_index++;

  // 1. Beräkna vilken frame som MÅSTE vara klar för att vi ska få återanvända
  // resurser. Om vi är på frame 3 och har 2 frames in flight, måste frame 1
  // vara klar.
  if (mgr->frame_index <= mgr->frames_in_flight) {
    return; // Bufferten är inte full än, bara kör.
  }

  uint64_t wait_value = mgr->frame_index - mgr->frames_in_flight;

  // 2. Kolla om GPU:n redan är klar (optimering för att slippa syscall)
  uint64_t current_val;
  vkGetSemaphoreCounterValue(mgr->device, mgr->timeline, &current_val);

  if (current_val < wait_value) {
    // 3. GPU ligger efter, vänta på hosten.
    VkSemaphoreWaitInfo waitInfo = {.sType =
                                        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                    .semaphoreCount = 1,
                                    .pSemaphores = &mgr->timeline,
                                    .pValues = &wait_value};
    vkWaitSemaphores(mgr->device, &waitInfo, UINT64_MAX);
  }
}

// -----------------------------------------------------------------------------
// GPU SUBMISSION
// -----------------------------------------------------------------------------

// is_last_in_frame: Sätt denna till true endast på sista submiten innan
// Present.
void submit_work(SubmitManager mgr, VkCommandBuffer cmd,
                 bool is_last_in_frame) {

  VkCommandBufferSubmitInfo cmd_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd};

  VkSemaphoreSubmitInfo signal_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = mgr->timeline,
      .value = mgr->frame_index, // Vi signalerar att "Nu är frame X klar"
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .deviceIndex = 0};

  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cmd_info,

      // VIKTIGT: Vi väntar ALDRIG på något här.
      // Eftersom det är samma kö, väntar operationerna automatiskt på varandra.
      .waitSemaphoreInfoCount = 0,

      // Vi signalerar ENDAST om det är sista biten av framen.
      .signalSemaphoreInfoCount = is_last_in_frame ? 1 : 0,
      .pSignalSemaphoreInfos = is_last_in_frame ? &signal_info : NULL};

  vkQueueSubmit2(mgr->queue, 1, &submit, VK_NULL_HANDLE);
}

// --- Private Functions ---
