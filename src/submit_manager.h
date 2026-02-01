#pragma once
#include "common.h"
#include "gpu/swapchain.h"

typedef struct M_Submit M_Submit;

typedef enum SmStage {
  SM_STAGE_NONE = 0,
  SM_STAGE_ALL_COMMANDS,
  SM_STAGE_COLOR_ATTACHMENT_OUTPUT,
  SM_STAGE_COMPUTE_SHADER,
  SM_STAGE_ALL_TRANSFER,
  SM_STAGE_VERTEX_INPUT,
  SM_STAGE_FRAGMENT_SHADER,
  SM_STAGE_EARLY_FRAGMENT_TESTS,
} SmStage;

// PUBLIC FUNCTIONS
void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swp, SmStage wait_stage);
void sm_add_signal(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage);
void sm_add_wait(M_Submit *mgr, VkSemaphore sem, u64 val, SmStage stage);
void sm_begin_frame(M_Submit *mgr);
void sm_destroy(M_Submit *mgr);
u64 sm_get_timeline_cpu(M_Submit *submit);
u64 sm_get_timeline_gpu(M_Submit *submit);
M_Submit *sm_init(VkDevice dev, VkQueue queue);
void sm_present(M_Submit *mgr, M_Swapchain *swp);
u64 sm_submit(M_Submit *mgr, VkCommandBuffer cmd, bool is_last_in_frame);
u64 sm_submit_empty(M_Submit *mgr, bool is_last_in_frame);
void sm_wait_idle(M_Submit *mgr);
// END PUBLIC FUNCTIONS
