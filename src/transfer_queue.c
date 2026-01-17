#include "common.h"

typedef struct InternalBufferWrite {
  bool is_free;
  void *data;
  u32 size;
  ResHandle handle;
} InternalBufferWrite;

typedef struct TransferQueue {
  u64 semp_value;
} TransferQueue;

u64 transfer_completed_value() {}

u64 transfer_allocate_signal_value() {}

void submit() {}

typedef struct StagingAllocator {
  u32 len;
  u32 capacity;
  void *gpu_ptr;
} StagingAllocator;

void _stage_copy_to_buffer() {}
