#include "submit_manager.h"
#include "common.h"
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

#define SM_MAX_DEP 16

typedef struct FrameState {
  u64 fence_value;
  u32 wait_count;
  VkSemaphoreSubmitInfo waits[SM_MAX_DEP];
  u32 signal_count;
  VkSemaphoreSubmitInfo signals[SM_MAX_DEP];
} FrameState;

struct M_Submit {
  VkDevice device;
  VkQueue queue;
  VkSemaphore timeline_sem;
  u32 current_frame_idx;
  u64 total_frames_submitted;
  FrameState frames[MAX_FRAMES_IN_FLIGHT];
};

// --- Private Prototypes ---
static void _add_dep(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage, bool is_signal);

static VkPipelineStageFlags2 _map_stage(SmStage stage);

// --- Public API ---

M_Submit *sm_init(VkDevice dev, VkQueue queue) {
  M_Submit *mgr = calloc(1, sizeof(M_Submit));
  mgr->device = dev;
  mgr->queue = queue;
  mgr->timeline_sem = vk_create_semp_timeline(mgr->device, "SubmitManager-Timeline");
  return mgr;
}

void sm_begin_frame(M_Submit *mgr) {
  FrameState *f = &mgr->frames[mgr->current_frame_idx];

  // Wait for the specific timeline value associated with this frame slot
  if (f->fence_value > 0) {
    VkSemaphoreWaitInfo wait = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                .semaphoreCount = 1,
                                .pSemaphores = &mgr->timeline_sem,
                                .pValues = &f->fence_value};
    vkWaitSemaphores(mgr->device, &wait, 10000000000);
  }
}

void sm_add_wait(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage) { _add_dep(mgr, sem, val, stage, false); }

void sm_add_signal(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage) { _add_dep(mgr, sem, val, stage, true); }

void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swp, SmStage wait_stage) {
  assert(swp);

  vkAcquireNextImage2KHR(mgr->device,
                         &(VkAcquireNextImageInfoKHR){
                             .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
                             .swapchain = swp->swapchain,
                             .timeout = UINT64_MAX,
                             .semaphore = swp->sem_acquire[mgr->current_frame_idx],
                             .deviceMask = 1,
                         },
                         &swp->current_img_idx);

  // Recognizable Vulkan stage: block the COLOR_ATTACHMENT_OUTPUT until image is ready
  sm_add_wait(mgr, swp->sem_acquire[mgr->current_frame_idx], 0, _map_stage(wait_stage));
}

u64 sm_get_timeline_gpu(M_Submit *submit) {
  u64 gpu_curr_ticket = 0;
  vkGetSemaphoreCounterValue(submit->device, submit->timeline_sem, &gpu_curr_ticket);
  return gpu_curr_ticket;
}
u64 sm_get_timeline_cpu(M_Submit *submit) {
  // the current frame, signal
  // because we add +1  at last submit
  return submit->total_frames_submitted + 1;
}

u64 sm_submit_empty(M_Submit *mgr, bool is_last_in_frame) {
    return sm_submit(mgr, VK_NULL_HANDLE, is_last_in_frame);
}

u64 sm_submit(M_Submit *mgr, VkCommandBuffer cmd, bool is_last_in_frame) {
    FrameState *f = &mgr->frames[mgr->current_frame_idx];
    
    // 1. Handle Frame Timing & Master Timeline
    if (is_last_in_frame) {
        mgr->total_frames_submitted++;
        f->fence_value = mgr->total_frames_submitted;
        
        // Signal completion to the CPU-GPU sync timeline
        sm_add_signal(mgr, mgr->timeline_sem, f->fence_value, SM_STAGE_ALL_COMMANDS);
    }

    // 2. Prepare Submit Info
    // If cmd is NULL, count is 0, and pCommandBufferInfos can be NULL.
    u32 cmd_count = (cmd != VK_NULL_HANDLE) ? 1 : 0;
    VkCommandBufferSubmitInfo cmd_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd
    };

    VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount   = f->wait_count,
        .pWaitSemaphoreInfos      = f->waits,
        .commandBufferInfoCount   = cmd_count,
        .pCommandBufferInfos      = (cmd_count > 0) ? &cmd_info : NULL,
        .signalSemaphoreInfoCount = f->signal_count,
        .pSignalSemaphoreInfos    = f->signals,
    };

    // 3. Execute on the Queue
    vkQueueSubmit2(mgr->queue, 1, &submit, VK_NULL_HANDLE);

    // 4. Reset counts for the next submission in this frame (or the next frame)
    f->wait_count = 0;
    f->signal_count = 0;

    // 5. Lifecycle Management
    u64 result = is_last_in_frame ? f->fence_value : 0;
    
    if (is_last_in_frame) {
        mgr->current_frame_idx = (mgr->current_frame_idx + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    return result;
}

void sm_present(M_Submit *mgr, M_Swapchain *swp) {
  vkQueuePresentKHR(mgr->queue, &(VkPresentInfoKHR){
                                    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &swp->sem_render_finished[swp->current_img_idx],
                                    .swapchainCount = 1,
                                    .pSwapchains = &swp->swapchain,
                                    .pImageIndices = &swp->current_img_idx,
                                });
}


void sm_wait_idle(M_Submit *mgr) { vkQueueWaitIdle(mgr->queue); }

void sm_destroy(M_Submit *mgr) {
  if (!mgr)
    return;
  sm_wait_idle(mgr);
  vkDestroySemaphore(mgr->device, mgr->timeline_sem, NULL);
  free(mgr);
}

// --- Private Functions ---

static void _add_dep(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage, bool is_signal) {
  FrameState *f = &mgr->frames[mgr->current_frame_idx];
  u32 *cnt = is_signal ? &f->signal_count : &f->wait_count;
  VkSemaphoreSubmitInfo *info = is_signal ? f->signals : f->waits;

  if (*cnt < SM_MAX_DEP) {
    info[(*cnt)++] = (VkSemaphoreSubmitInfo){.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                                             .semaphore = sem,
                                             .value = val,
                                             .stageMask = _map_stage(stage)};
  }
}

// --- Private ---
static VkPipelineStageFlags2 _map_stage(SmStage stage) {
  switch (stage) {
  case SM_STAGE_COLOR_ATTACHMENT_OUTPUT:
    return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  case SM_STAGE_COMPUTE_SHADER:
    return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
  case SM_STAGE_ALL_TRANSFER:
    return VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
  case SM_STAGE_VERTEX_INPUT:
    return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
  case SM_STAGE_FRAGMENT_SHADER:
    return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  case SM_STAGE_EARLY_FRAGMENT_TESTS:
    return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  case SM_STAGE_ALL_COMMANDS:
    return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  default:
    return VK_PIPELINE_STAGE_2_NONE;
  }
}
