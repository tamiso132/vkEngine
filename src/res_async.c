#include "res_async.h"
#include "command.h"
#include "common.h"
#include "resource/resmanager.h"
#include "transfer_queue.h"
#include "util.h"
#include <string.h>

// --- Private Prototypes ---

void async_init(M_Resource *rm, RGBufferInfo *info, AsyncBuffer *ab) {

  memset(ab, 0, sizeof(AsyncBuffer));

  ab->buffers[0] = rm_create_buffer(rm, info);
  ab->buffers[1] = rm_create_buffer(rm, info);
}

ResHandle async_get_backbuffer(AsyncBuffer *async) { return async->buffers[!async->active_b_index]; }
ResHandle async_get_active_buffer(AsyncBuffer *ab) { return ab->buffers[ab->active_b_index]; }
u32 async_get_active_desc_index(AsyncBuffer *ab, M_Resource *rm) {
  return rm_get_buffer_descriptor_index(rm, async_get_active_buffer(ab));
}
void async_destroy(M_Resource *rm, AsyncBuffer *ab) { LOG_ERROR("NOT IMPLEMENTED"); }
void async_swap(M_Resource *rm, AsyncBuffer *ab) { ab->active_b_index = !ab->active_b_index; }
// --- Private Functions ---
