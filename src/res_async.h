#pragma once
#include "resmanager.h"

// PUBLIC FUNCTIONS
void async_init(ResourceManager *rm, AsyncBuffer *ab, uint32_t bindless_id);
void async_sync(ResourceManager *rm, AsyncBuffer *ab);
