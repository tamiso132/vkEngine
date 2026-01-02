#include "res_async.h"
#include "command.h"
#include "resmanager.h"
#include <string.h>
typedef struct AsyncBuffer {
  // front and back buffer
  ResHandle buffers[2];
  u32 active_b_index;
  VkFence upload_fence;
  bool is_uploading;
} AsyncBuffer;

// --- Private Prototypes ---
static void _async_swap(ResourceMa *rm, AsyncBuffer *ab);

void async_init(ResourceMa *rm, RGBufferInfo *info, AsyncBuffer *ab) {

  memset(ab, 0, sizeof(AsyncBuffer) * 2);

  ab->buffers[0] = rm_create_buffer(rm, info);
  ab->buffers[1] = rm_create_buffer(rm, info);
  ab->is_uploading = false;
  ab->upload_fence = NULL;
}

void async_check_upload_ready(ResourceMa *rm, AsyncBuffer *ab) {
  if (!ab->is_uploading)
    return;

  if (vkGetFenceStatus(rm_get_gpu(rm)->device, ab->upload_fence) == VK_SUCCESS) {
    _async_swap(rm, ab);
  }
}

void async_update(ResourceMa *rm, CmdBuffer cmd, AsyncBuffer *ab, VkFence *fence, void *data, uint64_t size) {
  async_check_upload_ready(rm, ab);
  u32 inactive = !((bool)ab->active_b_index);

  if (ab->is_uploading)
    return; // Still busy with
            // previous update
  cmd_buffer_upload(cmd, rm, ab->buffers[inactive], data, size);
  // Acquire from global pool (recycles retired memory)
  ab->is_uploading = true;
}

ResHandle async_get_active_handle(AsyncBuffer *ab) { return ab->buffers[ab->active_b_index]; }

// --- Private Functions ---

static void _async_swap(ResourceMa *rm, AsyncBuffer *ab) {
  ab->is_uploading = false;
  ab->active_b_index = !ab->active_b_index;
}
