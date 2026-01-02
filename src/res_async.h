#pragma once
#include "command.h"
#include "resmanager.h"
typedef struct AsyncBuffer AsyncBuffer;
// PUBLIC FUNCTIONS

ResHandle async_get_active_handle(AsyncBuffer *ab);

void async_check_upload_ready(M_Resource *rm, AsyncBuffer *ab);
void async_init(M_Resource *rm, RGBufferInfo *info, AsyncBuffer *ab);
void async_update(M_Resource *rm, CmdBuffer cmd, AsyncBuffer *ab, VkFence *fence, void *data, uint64_t size);
