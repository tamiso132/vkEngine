#pragma once
#include "common.h"
#include "gpu/swapchain.h"

typedef struct M_Submit M_Submit;

// PUBLIC FUNCTIONS
void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain);
void sm_add_signal_binary(M_Submit *mgr, VkSemaphore sem, VkPipelineStageFlags2 stageMask);
void sm_add_signal_timeline(M_Submit *mgr, VkSemaphore timeline, u64 value, VkPipelineStageFlags2 stageMask);
void sm_add_wait_binary(M_Submit *mgr, VkSemaphore sem, VkPipelineStageFlags2 stageMask);

void sm_add_wait_timeline(M_Submit *mgr, VkSemaphore timeline, u64 value, VkPipelineStageFlags2 stageMask);
void sm_begin_frame(M_Submit *mgr);
void sm_clear_extra_submit(M_Submit *mgr);
u64 sm_get_cpu_ticket(M_Submit *mgr);
u64 sm_get_gpu_ticket(M_Submit *mgr);
M_Submit *sm_init(VkDevice dev, VkQueue queue);
bool sm_is_done(M_Submit *mgr, u64 timeline_index);
void sm_on_frame_end(M_Submit *mgr);
void sm_present(M_Submit *mgr, M_Swapchain *swapchain);
void sm_wait(M_Submit *mgr, u64 timeline_index);
u64 sm_work(M_Submit *mgr, M_Swapchain *swapchains, u32 swapchain_count, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit);
void sm_work_manual(M_Submit *mgr, VkCommandBuffer cmd);
// END PUBLIC FUNCTIONS
