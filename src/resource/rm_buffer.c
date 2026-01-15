//! rm_internal.h
#include "rm_internal.h"

ResHandle rm_buffer_create(M_Resource *rm, M_GPU *gpu, RGBufferInfo *info) {
  RBuffer buffer = {
      .sync = {.access = VK_ACCESS_2_NONE,
               .stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
               .layout = VK_IMAGE_LAYOUT_UNDEFINED},
      .mem = info->mem,
      .usage = (VkBufferUsageFlags)info->usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      .capacity = info->capacity,
      .binding = RES_B_STORAGE_BUFFER,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
  };
  strncpy(buffer.name, info->name ? info->name : "Buffer", sizeof(buffer.name) - 1);
  buffer.name[sizeof(buffer.name) - 1] = 0;

  VkBufferCreateInfo ci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = info->capacity, .usage = buffer.usage};

  VmaAllocationCreateInfo ai = {0};
  // Keep your old behavior: hard-require flags
  ai.requiredFlags = info->mem;

  VkResult r = vmaCreateBuffer(gpu->allocator, &ci, &ai, &buffer.handle, &buffer.alloc, NULL);
  vk_check(r);

  u32 id = (u32)vec_len(&rm->resources[RES_TYPE_BUFFER]);
  ResHandle h = {.id = id, .res_type = RES_TYPE_BUFFER};
  vec_push(&rm->resources[RES_TYPE_BUFFER], &buffer);

  VkDescriptorBufferInfo descriptorInfo = {0};
  descriptorInfo.buffer = buffer.handle;
  descriptorInfo.range = VK_WHOLE_SIZE;

  rm_bindless_add(rm, h, NULL, &descriptorInfo);

  return h;
}
