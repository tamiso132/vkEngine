//! rm_internal.h
#include "rm_internal.h"

void rm_retire_buffer(M_Resource *rm, ResHandle handle) {
  RBuffer *buffer = rm_get_buffer_internal(rm, handle);

  RetiredRes rb = {0};
  rb.frame_retired = rm->frame_count;
  rb.alloc = buffer->alloc;
  rb.type = handle.res_type;
  rb.buffer.handle = buffer->handle;

  vec_push(&rm->retired_res, &rb);

  // Clear live handle so double-destroy can’t happen
  buffer->handle = VK_NULL_HANDLE;
  buffer->alloc = NULL;
}

void rm_retire_image(M_Resource *rm, ResHandle handle) {
  RImage *image = rm_get_image_internal(rm, handle);
  if (image->is_imported)
    return; // imported images should not be retired/destroyed here

  RetiredRes rb = {0};
  rb.frame_retired = rm->frame_count;
  rb.alloc = image->alloc;
  rb.type = handle.res_type;
  rb.image.handle = image->handle;
  rb.image.view = image->view; // FIX: store view too

  vec_push(&rm->retired_res, &rb);

  image->handle = VK_NULL_HANDLE;
  image->view = VK_NULL_HANDLE;
  image->alloc = NULL;
}

void rm_retire_on_new_frame(M_Resource *rm, M_GPU *gpu, u32 frames_in_flight) {
  // anything retired <= (frame_count - frames_in_flight) is safe
  u32 safe_frame = (rm->frame_count > frames_in_flight) ? (rm->frame_count - frames_in_flight) : 0;

  for (int i = 0; i < vec_len(&rm->retired_res); i++) {
    RetiredRes *r = VEC_AT(&rm->retired_res, i, RetiredRes);

    if (r->frame_retired < safe_frame) {
      if (r->type == RES_TYPE_BUFFER) {
        vmaDestroyBuffer(gpu->allocator, r->buffer.handle, r->alloc);
      } else if (r->type == RES_TYPE_IMAGE) {
        if (r->image.view)
          vkDestroyImageView(gpu->device, r->image.view, NULL);
        vmaDestroyImage(gpu->allocator, r->image.handle, r->alloc);
      }
      vec_remove_at(&rm->retired_res, i);
      i--;
    }
  }
}
