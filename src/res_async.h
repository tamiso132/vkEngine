#pragma once
#include "resmanager.h"

typedef struct AsyncBuffer AsyncBuffer;
// PUBLIC FUNCTIONS

void async_check_upload_ready(ResourceManager *rm, AsyncBuffer *ab);
void async_init(ResourceManager *rm, RGBufferInfo *info, AsyncBuffer *ab);
void async_sync(ResourceManager *rm, AsyncBuffer *ab);
void async_update(ResourceManager *rm, VkCommandBuffer cmd, AsyncBuffer *ab,
                  VkFence *fence, void *data, uint64_t size);
