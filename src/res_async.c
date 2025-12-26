#include "res_async.h"

void async_init(ResourceManager *rm, AsyncBuffer *ab, uint32_t bindless_id) {
  ab->bindless_id = bindless_id;
  ab->front = (RGHandle){0};
  ab->back = (RGHandle){0};
  ab->is_uploading = false;

  VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  vkCreateFence(rm->gpu->device, &fi, NULL, &ab->upload_fence);
}

// Internal: Call this once a transfer fence is signaled
static void _async_swap(ResourceManager *rm, AsyncBuffer *ab) {
  if (ab->front.id != 0) {
    rm_retire_buffer(rm, ab->front); // Send old buffer to global "cool down"
  }

  ab->front = ab->back;
  ab->back = (RGHandle){0};
  ab->is_uploading = false;
  vkResetFences(rm->gpu->device, 1, &ab->upload_fence);

  // Update the Bindless Slot to point to the new memory
  _rm_update_bindless_buffer(rm, ab->bindless_id, rm_get_buffer(rm, ab->front));
}

void async_sync(ResourceManager *rm, AsyncBuffer *ab) {
  if (!ab->is_uploading)
    return;
  if (vkGetFenceStatus(rm->gpu->device, ab->upload_fence) == VK_SUCCESS) {
    _async_swap(rm, ab);
  }
}

void async_update(ResourceManager *rm, AsyncBuffer *ab, void *data,
                  uint64_t size) {
  async_sync(rm, ab);
  if (ab->is_uploading)
    return; // Still busy with previous update

  // Acquire from global pool (recycles retired memory)
  ab->back = rm_acquire_reusable_buffer(rm, size,
                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT);

  gpu_upload_async(rm->gpu, ab->back, data, size, ab->upload_fence);
  ab->is_uploading = true;
}
