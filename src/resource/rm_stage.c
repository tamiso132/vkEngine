//! rm_internal.h
#include "rm_internal.h"

VkDeviceSize rm_align_up(VkDeviceSize v, VkDeviceSize a) {
  if (a == 0)
    return v;
  return (v + (a - 1)) & ~(a - 1);
}

void rm_stage_init(M_Resource *rm, M_GPU *gpu, VkDeviceSize capacity_bytes) {
  rm->stage.capacity = capacity_bytes;
  rm->stage.cur_frame = 0;
  rm->stage.is_coherent = false;

  for (u32 i = 0; i < 1; ++i) {
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity_bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    VmaAllocationCreateInfo ai = {0};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo outInfo = {0};
    VkResult res = vmaCreateBuffer(gpu->allocator, &bi, &ai, &rm->stage.buffer[i], &rm->stage.alloc[i], &outInfo);
    vk_check(res);

    rm->stage.mapped[i] = outInfo.pMappedData;
    rm->stage.offset[i] = 0;
  }
}

void rm_stage_destroy(M_Resource *rm, M_GPU *gpu) {
  for (u32 i = 0; i < 1; ++i) {
    if (rm->stage.buffer[i]) {
      vmaDestroyBuffer(gpu->allocator, rm->stage.buffer[i], rm->stage.alloc[i]);
    }
  }
  memset(&rm->stage, 0, sizeof(rm->stage));
}

void rm_stage_on_new_frame(M_Resource *rm) {
  rm->stage.cur_frame = (rm->stage.cur_frame + 1) % 1;
  rm->stage.offset[rm->stage.cur_frame] = 0; // FIX: actually reset
}

RmStageSlice rm_stage_push(M_Resource *rm, M_GPU *gpu, const void *data, VkDeviceSize size, VkDeviceSize alignment) {
  u32 fi = rm->stage.cur_frame;

  VkDeviceSize off = rm_align_up(rm->stage.offset[fi], alignment ? alignment : 16);
  VkDeviceSize end = off + size;

  assert(end <= rm->stage.capacity && "RM staging overflow");
  rm->stage.offset[fi] = end;

  void *dst = (u8 *)rm->stage.mapped[fi] + off;
  memcpy(dst, data, (size_t)size);

  if (!rm->stage.is_coherent) {
    vmaFlushAllocation(gpu->allocator, rm->stage.alloc[fi], off, size);
  }

  return (RmStageSlice){
      .buffer = rm->stage.buffer[fi],
      .offset = off,
      .size = size,
  };
}
