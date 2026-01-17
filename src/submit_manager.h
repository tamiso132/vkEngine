#pragma once

#include "gpu/swapchain.h"
#include <stdbool.h>
#include <volk.h>
// Opaque handles - döljer implementationen för användaren
typedef struct TimelineHandle TimelineHandle;

// -----------------------------------------------------------------------------
// KONFIGURATION
// -----------------------------------------------------------------------------

typedef struct {
  VkCommandBuffer cmd;

  TimelineHandle *wait_timelines;
  uint32_t wait_count;
  VkPipelineStageFlags2 *wait_stages; // T.ex. VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT

} SubmitInfo;

// PUBLIC FUNCTIONS

M_Submit *init_submit(VkDevice device, VkQueue queue, bool is_present, uint32_t frames_in_flight);
SystemFunc sm_system_get_func();
u64 sm_get_cpu_signal_count(M_Submit *mgr);
u64 sm_get_gpu_signal_count(M_Submit *mgr);
//
u64 sm_work(M_Submit *mgr, M_Swapchain *swapchain, VkCommandBuffer cmd, bool is_last_in_frame, bool is_first_submit);
//
void sm_acquire_swapchain(M_Submit *mgr, M_Swapchain *swapchain);
void sm_begin_frame(M_Submit *mgr);
void sm_present(M_Submit *mgr, M_Swapchain *swapchain);
// END PUBLIC FUNCTIONS
