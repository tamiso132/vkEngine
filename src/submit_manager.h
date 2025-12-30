#pragma once

#include <stdbool.h>
#include <volk.h>

// Opaque handles - döljer implementationen för användaren
typedef struct SubmitManager_T *SubmitManager;
typedef struct TimelineHandle_T *TimelineHandle;

// -----------------------------------------------------------------------------
// KONFIGURATION
// -----------------------------------------------------------------------------

typedef struct {
  VkQueue queue;
  const char *debug_name;

  // Hur många submits (max) görs till denna kö per frame?
  uint32_t max_submits_per_frame;

} TimelineConfig;

typedef struct {
  VkCommandBuffer cmd;

  TimelineHandle *wait_timelines;
  uint32_t wait_count;
  VkPipelineStageFlags2
      *wait_stages; // T.ex. VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT

} SubmitInfo;

// PUBLIC FUNCTIONS

SubmitManager submit_manager_create(VkDevice device, VkQueue queue,
                                    uint32_t frames_in_flight);

void submit_manager_destroy(SubmitManager mgr);

void submit_begin_frame(SubmitManager mgr);

void submit_work(SubmitManager mgr, VkCommandBuffer cmd, bool is_last_in_frame);
