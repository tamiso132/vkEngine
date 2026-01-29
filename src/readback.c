#include "readback.h"

typedef u32 uvec4[4];
typedef u32 uvec2[2];

// --- Private Prototypes ---

void readback_init(ReadBackBuffer *self, M_Resource *rm, VkExtent2D extent) {

  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  u32 tot_pixels = extent.height * extent.width;

  RGBufferInfo info = {.capacity = sizeof(DbgPixelBlock) * tot_pixels,
                       .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                       .queue_type = BUFFER_QUEUE_GRAPHIC,
                       .name = "Buffer-Readback",
                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};

  self->buffer = rm_create_buffer(rm, &info);
  RBuffer *buffer = rm_get_buffer(rm, self->buffer);

  vmaMapMemory(dev->allocator, buffer->alloc, &self->p_cpu);
}

DbgPixelBlock *readback_get_cpu_ptr(ReadBackBuffer *self) { return self->p_cpu; }

u32 readback_get_push_id(ReadBackBuffer *self, M_Resource *rm) {
  return rm_get_buffer(rm, self->buffer)->bindlessIndex;
}
// --- Private Functions ---
