#pragma once
#include "command.h"
#include "resmanager.h"
typedef struct AsyncBuffer AsyncBuffer;
// PUBLIC FUNCTIONS

ResHandle async_get_active_handle(AsyncBuffer *ab);

void async_check_upload_ready(ResourceMa *rm, AsyncBuffer *ab);
void async_init(ResourceMa *rm, RGBufferInfo *info, AsyncBuffer *ab);
void async_sync(ResourceMa *rm, AsyncBuffer *ab);
void async_update(ResourceMa *rm, CmdBuffer cmd, AsyncBuffer *ab, VkFence *fence, void *data, uint64_t size);
