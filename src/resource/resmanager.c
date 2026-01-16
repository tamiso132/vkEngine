//! rm_internal.h
#include "common.h"
#include "rm_internal.h"

// System lifecycle glue

// Keep consistent with your engine (looks like MAX_FRAMES_IN_FLIGHT = 3 usage)
#ifndef RM_FRAMES_IN_FLIGHT
#define RM_FRAMES_IN_FLIGHT 3
#endif

// --- Private Prototypes ---
static void _system_destroy(void);
static bool _system_init(void *config, u32 *mem_req);

SystemFunc rm_system_get_func() {
  return (SystemFunc){
      .on_init = _system_init,
      .on_update = NULL,
      .on_shutdown = _system_destroy,
  };
}

ResHandle rm_create_buffer(M_Resource *rm, RGBufferInfo *info) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  return rm_buffer_create(rm, gpu, info);
}

ResHandle rm_create_image(M_Resource *rm, RGImageInfo info) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  return rm_image_create(rm, gpu, info);
}

RBuffer *rm_get_buffer(M_Resource *rm, ResHandle handle) {
  assert(handle.res_type == RES_TYPE_BUFFER);
  return VEC_AT(&rm->resources[handle.res_type], handle.id, RBuffer);
}
RImage *rm_get_image(M_Resource *rm, ResHandle handle) {
  assert(handle.res_type == RES_TYPE_IMAGE);
  return VEC_AT(&rm->resources[handle.res_type], handle.id, RImage);
}

void rm_resize_image(M_Resource *rm, ResHandle handle, uint32_t width, uint32_t height) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  // retire old owned resources (view + image)
  rm_retire_image(rm, handle);

  RImage *image = rm_get_image_internal(rm, handle);
  image->extent.width = width;
  image->extent.height = height;

  rm_create_image_full(rm, gpu, image);

  // Update bindless descriptor with new view
  VkDescriptorImageInfo imageInfo = {
      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
      .imageView = image->view,
      .sampler = (image->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? rm->default_sampler : VK_NULL_HANDLE};
  rm_bindless_update(rm, handle, &imageInfo, NULL);
}

void rm_import_existing_image(M_Resource *rm, ResHandle handle, VkImage raw_img, VkImageView view,
                              VkExtent2D new_extent, bool delete_img) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  RImage *img = rm_get_image_internal(rm, handle);

  rm_reset_image_sync(img);

  // Destroy old view
  if (img->view) {
    vkDestroyImageView(gpu->device, img->view, NULL);
    img->view = VK_NULL_HANDLE;
  }

  // Destroy old owned image if requested
  if (delete_img && img->handle) {
    vmaDestroyImage(gpu->allocator, img->handle, img->alloc);
    img->handle = VK_NULL_HANDLE;
    img->alloc = NULL;
  }

  img->extent = new_extent;
  img->handle = raw_img;
  img->view = view;
  img->is_imported = true;

  // Update bindless
  //   VkDescriptorImageInfo imageInfo = {
  //       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  //       .imageView = img->view,
  //       .sampler = (img->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? rm->default_sampler : VK_NULL_HANDLE};

  //   rm_bindless_update(rm, handle, &imageInfo, NULL);
}

ResHandle rm_import_image(M_Resource *rm, RGImageInfo *info, VkImage img, VkImageView view) {
  return rm_image_import(rm, info, img, view);
}

void rm_on_new_frame(M_Resource *rm) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  // advance frame counter first
  rm->frame_count++;

  rm_retire_on_new_frame(rm, gpu, RM_FRAMES_IN_FLIGHT);
  rm_stage_on_new_frame(rm);
}

void rm_destroy(M_Resource *rm) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  // Stage
  rm_stage_destroy(rm, gpu);

  // Retired resources (force free)
  // (If you want strict GPU-idle, do vkDeviceWaitIdle before this)
  for (u32 i = 0; i < (u32)vec_len(&rm->retired_res); i++) {
    RetiredRes *r = VEC_AT(&rm->retired_res, i, RetiredRes);
    if (r->type == RES_TYPE_BUFFER) {
      vmaDestroyBuffer(gpu->allocator, r->buffer.handle, r->alloc);
    } else if (r->type == RES_TYPE_IMAGE) {
      if (r->image.view)
        vkDestroyImageView(gpu->device, r->image.view, NULL);
      vmaDestroyImage(gpu->allocator, r->image.handle, r->alloc);
    }
  }
  vec_free(&rm->retired_res);

  // Buffers
  Vector *buffers = &rm->resources[RES_TYPE_BUFFER];
  for (u32 i = 0; i < (u32)vec_len(buffers); i++) {
    RBuffer *b = VEC_AT(buffers, i, RBuffer);
    if (b->handle)
      vmaDestroyBuffer(gpu->allocator, b->handle, b->alloc);
  }

  // Images
  Vector *images = &rm->resources[RES_TYPE_IMAGE];
  for (u32 i = 0; i < (u32)vec_len(images); i++) {
    RImage *im = VEC_AT(images, i, RImage);
    rm_image_destroy_owned(rm, gpu, im);
    if (im->name)
      free(im->name);
    im->name = NULL;
  }

  for (u32 i = 0; i < RES_TYPE_COUNT; i++)
    vec_free(&rm->resources[i]);

  // Bindless
  rm_bindless_shutdown(rm, gpu);

  memset(rm, 0, sizeof(*rm));
}

u32 rm_get_buffer_descriptor_index(M_Resource *rm, ResHandle buffer) {
  return rm_get_buffer_internal(rm, buffer)->bindlessIndex;
}

u32 rm_get_buffer_index(M_Resource *rm, ResHandle buffer) { return rm_get_buffer_internal(rm, buffer)->bindlessIndex; }

u32 rm_get_image_index(M_Resource *rm, ResHandle image) { return rm_get_image_internal(rm, image)->bindlessIndex; }

VkDescriptorSetLayout rm_get_bindless_layout(M_Resource *rm) { return rm->bindless_layout; }
VkDescriptorSet rm_get_bindless_set(M_Resource *rm) { return rm->bindless_set; }
VkPipelineLayout rm_get_pipeline_layout(M_Resource *rm) { return rm->pip_layout; }

// ----- staging (public) -----

RmStageSlice rm_get_stage_buffer(M_Resource *rm, const void *data, VkDeviceSize size, VkDeviceSize alignment) {
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  assert(data && size > 0);
  return rm_stage_push(rm, gpu, data, size, alignment);
}

// --- Private Functions ---

static void _system_destroy(void) {
  M_Resource *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  rm_destroy(rm);
}

// -------------------- system init/shutdown --------------------
static bool _system_init(void *config, u32 *mem_req) {
  SYSTEM_HELPER_MEM(mem_req, M_Resource);

  M_Resource *rm = SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  M_GPU *gpu = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  memset(rm, 0, sizeof(*rm));

  vec_init(&rm->resources[RES_TYPE_IMAGE], sizeof(RImage), NULL);
  vec_init(&rm->resources[RES_TYPE_BUFFER], sizeof(RBuffer), NULL);
  vec_init(&rm->retired_res, sizeof(RetiredRes), NULL);

  rm_bindless_init(rm, gpu);
  rm_stage_init(rm, gpu, MIB(100));

  rm->frame_count = 0;
  memset(rm->b_counter, 0, sizeof(rm->b_counter));

  return true;
}
