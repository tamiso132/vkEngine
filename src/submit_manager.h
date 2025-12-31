#pragma once

#include "gpu/swapchain.h"
#include <stdbool.h>
#include <volk.h>
// Opaque handles - döljer implementationen för användaren
typedef struct M_SubmitManager M_SubmitManager;
typedef struct TimelineHandle TimelineHandle;

// -----------------------------------------------------------------------------
// KONFIGURATION
// -----------------------------------------------------------------------------

typedef struct {
  VkCommandBuffer cmd;

  TimelineHandle *wait_timelines;
  uint32_t wait_count;
  VkPipelineStageFlags2
      *wait_stages; // T.ex. VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT

} SubmitInfo;

// PUBLIC FUNCTIONS

M_SubmitManager *submit_manager_create(VkDevice device, VkQueue queue,
                                       uint32_t frames_in_flight);

void submit_manager_destroy(M_SubmitManager *mgr);

void submit_begin_frame(M_SubmitManager *mgr);

void submit_acquire_swapchain(M_SubmitManager *mgr, GPUSwapchain *swapchain);

void submit_work(M_SubmitManager *mgr, GPUSwapchain *swapchain,
                 VkCommandBuffer cmd, bool is_last_in_frame,
                 bool is_first_submit);

void submit_present(M_SubmitManager *mgr, GPUSwapchain *swapchain);
