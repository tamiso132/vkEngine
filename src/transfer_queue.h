#pragma once

#include <volk.h>

#include "common.h"
#include <vk_mem_alloc.h>

typedef struct TransferQueue TransferQueue;
typedef u32 Ticket;
// PUBLIC FUNCTIONS

Ticket transfer_get_current_ticket_completed(TransferQueue *transfer);
Ticket transfer_push_upload(TransferQueue *transfer, M_Resource *rm, ResHandle handle, u32 size, u32 aligment);
TransferQueue *init_queue(VkDevice device, VkQueue transfer, u32 queue_fam, VmaAllocator allocator, u64 capacity,
                          u32 max_frame_in_flight);
void transfer_on_new_frame(TransferQueue *transfer);
void transfer_submit_on_frame_end(TransferQueue *transfer);
// END PUBLIC FUNCTIONS
