#include "transfer_queue.h"
#include "command.h"
#include "common.h"
#include "resource/rm_internal.h"
#include "resource/staging_arena.h"
#include "submit_manager.h"
#include "util.h"
#include "vector.h"

typedef struct {
  CmdBuffer cmd;
  u32 submit_signal;
} Frame;

typedef struct TransferQueue {
  StagingGrowRing *staging_ring;
  M_Submit *submit;
  VkDevice device;
  VkQueue transfer;
  VmaAllocator allocator;
  bool is_cmd_on;
  u32 max_frames_in_flight;
  u32 frame_index;
  bool in_flight;
  bool is_frame_started;
  Frame frames[];
} TransferQueue;

// --- Private Prototypes ---
static Frame _get_frame(TransferQueue *transfer);

bool transfer_in_flight(TransferQueue *transfer) { return transfer->in_flight; }

TransferQueue *transfer_init(VkDevice device, VkQueue transfer, u32 queue_fam, VmaAllocator allocator, u64 capacity,
                             u32 max_frame_in_flight) {

  TransferQueue *tq = calloc(1, sizeof(TransferQueue) + sizeof(Frame) * max_frame_in_flight);
  tq->submit = sm_init(device, transfer);
  tq->staging_ring = sgr_init(allocator, capacity, max_frame_in_flight, true);
  tq->device = device;
  tq->transfer = transfer;
  tq->allocator = allocator;
  tq->max_frames_in_flight = max_frame_in_flight;
  tq->is_frame_started = false;

  for (u32 i = 0; i < max_frame_in_flight; i++) {
    tq->frames[i].cmd = cmd_init(device, queue_fam);
  }

  return tq;
}

// LATER ON; SHOULD PROBABLY COPY OVER
Ticket transfer_push_upload(TransferQueue *transfer, M_Resource *rm, ResHandle handle, u32 size, void *data,
                            u32 aligment) {
  assert(transfer->is_frame_started);
  StagingSlice slice = sgr_alloc(transfer->staging_ring, size, aligment);

  if (!transfer->is_cmd_on) {
    cmd_begin(transfer->device, _get_frame(transfer).cmd);
    transfer->is_cmd_on = true;
  }
  cmd_buffer_copy(_get_frame(transfer).cmd, rm, transfer->allocator, handle, slice);
  return sm_get_cpu_ticket(transfer->submit);
}

void transfer_on_new_frame(TransferQueue *transfer) {
  u32 old_frame_index = transfer->frame_index;
  transfer->frame_index = (old_frame_index + 1) % transfer->max_frames_in_flight;
  if (sm_get_gpu_ticket(transfer->submit) >= _get_frame(transfer).submit_signal) {
    cmd_begin(transfer->device, _get_frame(transfer).cmd);
    transfer->is_cmd_on = true;

    sgr_on_new_frame(transfer->staging_ring, transfer->frame_index);
    sm_begin_frame(transfer->submit);
    transfer->in_flight = false;
    transfer->is_frame_started = true;
    return;
  }
}

Ticket transfer_get_current_ticket_completed(TransferQueue *transfer) {
  return sm_get_gpu_ticket(transfer->submit);
}

void transfer_submit_on_frame_end(TransferQueue *transfer) {

  if (!transfer->is_cmd_on)
    return;

  cmd_end(transfer->device, _get_frame(transfer).cmd);
  transfer->is_cmd_on = false;

  sgr_flush(transfer->staging_ring, transfer->frame_index);

  transfer->frames[transfer->frame_index].submit_signal =
      sm_work(transfer->submit, NULL, 0, _get_frame(transfer).cmd.buffer, true, true);


  sm_on_frame_end(transfer->submit);
  transfer->in_flight = true;
  transfer->is_frame_started = false;
}

// --- Private Functions ---

static Frame _get_frame(TransferQueue *transfer) { return transfer->frames[transfer->frame_index]; }
