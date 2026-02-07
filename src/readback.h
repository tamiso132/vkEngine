#pragma once

#include "common.h"
#include <cglm/cglm.h>
#include <volk.h>

#include "debug_ui/debug_inspector.h"
#include "shaders/rt/rt_shared.glsl"

typedef struct ReadBackBuffer {
  ResHandle buffer; // used by gpu
  u32 *p_cpu;
  VkExtent2D extent;
} ReadBackBuffer;

// PUBLIC FUNCTIONS
ResHandle readback_get_handle(ReadBackBuffer *self);
u32 readback_get_push_id(ReadBackBuffer *self, M_Resource *rm);
void readback_init(ReadBackBuffer *self, M_Resource *rm, VkExtent2D extent);
void readback_write_to_editor(ReadBackBuffer *self, EditorPixelEditor *editor);
// END PUBLIC FUNCTIONS
