#pragma once

#include <volk.h>

#include "common.h"
#include "vector.h"
#include <vk_mem_alloc.h>

typedef struct StagingSlice {
  VkBuffer buffer;
  VkDeviceSize offset;
  VkDeviceSize size;
  void *cpu_ptr; // mapped pointer to slice start
} StagingSlice;

typedef struct StagingGrowRing StagingGrowRing;

// PUBLIC FUNCTIONS

StagingGrowRing *sgr_init(VmaAllocator vma, VkDeviceSize initial_capacity, u32 frames_in_flight, bool allow_grow);
StagingSlice sgr_alloc(StagingGrowRing *sgr, VkDeviceSize size, VkDeviceSize alignment);
VkDeviceSize sgr_active_free_bytes(const StagingGrowRing *sgr);
VkDeviceSize sgr_active_used_bytes(const StagingGrowRing *sgr);
void sgr_destroy(StagingGrowRing *sgr);
void sgr_flush(StagingGrowRing *sgr, u32 slot);
void sgr_flush_range(StagingGrowRing *sgr, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);
void sgr_on_new_frame(StagingGrowRing *sgr, u32 slot);
void sgr_set_max_total_capacity(StagingGrowRing *sgr, VkDeviceSize max_total);
// END PUBLIC FUNCTIONS
