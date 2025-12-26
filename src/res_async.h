#pragma once
#include "resmanager.h"

// FUNC_START
void async_init(ResourceManager *rm, AsyncBuffer *ab, uint32_t bindless_id);
void async_sync(ResourceManager *rm, AsyncBuffer *ab);
// FUNC_END
