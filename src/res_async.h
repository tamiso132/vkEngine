#pragma once
#include "command.h"

typedef struct AsyncBuffer {
  // front and back buffer
  ResHandle buffers[2];
  u32 active_b_index;
} AsyncBuffer;

// PUBLIC FUNCTIONS

ResHandle async_get_active_buffer(AsyncBuffer *ab);
u32 async_get_active_desc_index(AsyncBuffer *ab, M_Resource *rm);
ResHandle async_get_backbuffer(AsyncBuffer *async);
void async_init(M_Resource *rm, RGBufferInfo *info, AsyncBuffer *ab);
void async_swap(M_Resource *rm, AsyncBuffer *ab);

// END PUBLIC FUNCTIONS
