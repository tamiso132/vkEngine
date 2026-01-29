#pragma once
#include "common.h"
#include "gpu/swapchain.h"

typedef struct M_Submit M_Submit;

// PUBLIC FUNCTIONS
M_Submit* sm_init(VkDevice dev, VkQueue queue);
void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain);
void sm_begin_frame(M_Submit *mgr);
bool sm_is_done(M_Submit *mgr, u64 timeline_index);
void sm_present(M_Submit *mgr, M_Swapchain *swapchain);
void sm_on_frame_end(M_Submit *mgr);
void sm_wait(M_Submit *mgr, u64 timeline_index);
u64 sm_work(M_Submit *mgr, M_Swapchain *swapchains, u32 swapchain_count, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit) ;
u64 sm_get_cpu_ticket(M_Submit *mgr);
u64 sm_get_gpu_ticket(M_Submit *mgr);
// END PUBLIC FUNCTIONS
