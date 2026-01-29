#include "common.h"
#include "resource/resmanager.h"
#include <cglm/cglm.h>

#include "shaders/rt/rt_shared.glsl"

typedef struct ReadBackBuffer {
  ResHandle buffer; // used by gpu
  void *p_cpu;
} ReadBackBuffer;

// PUBLIC FUNCTIONS
DbgPixelBlock *readback_get_cpu_ptr(ReadBackBuffer *self);
u32 readback_get_push_id(ReadBackBuffer *self, M_Resource *rm);
void readback_init(ReadBackBuffer *self, M_Resource *rm, VkExtent2D extent);
// END PUBLIC FUNCTIONS
