#pragma once
#include "resmanager.h"

typedef struct AsyncBuffer {
  ResHandle front;      // Buffer currently visible to GPU
  ResHandle back;       // Buffer currently being uploaded to
  VkFence upload_fence; // Signal for transfer completion
  uint32_t bindless_id; // Fixed index in the global descriptor array
  bool is_uploading;
} AsyncBuffer;

// PUBLIC FUNCTIONS
void async_init(ResourceManager *rm, AsyncBuffer *ab, uint32_t bindless_id);
void async_sync(ResourceManager *rm, AsyncBuffer *ab);
void async_update(ResourceManager *rm, AsyncBuffer *ab, void *data,
                  uint64_t size);
