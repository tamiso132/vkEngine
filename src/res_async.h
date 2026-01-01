#pragma once
#include "command.h"
#include "resmanager.h"
typedef struct AsyncBuffer AsyncBuffer;
// PUBLIC FUNCTIONS

ResHandle async_get_active_handle(AsyncBuffer *ab);

void async_check_upload_ready(ResourceManager *rm, AsyncBuffer *ab);
void async_init(ResourceManager *rm, RGBufferInfo *info, AsyncBuffer *ab);
void async_sync(ResourceManager *rm, AsyncBuffer *ab);
void async_update(ResourceManager *rm, CmdBuffer cmd, AsyncBuffer *ab, VkFence *fence, void *data, uint64_t size);
