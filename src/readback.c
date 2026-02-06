#include "readback.h"
#include "common.h"
#include "debug_ui/debug_inspector.h"
#include "resource/resmanager.h"
#include "rt/rt_shared.glsl"
#include "util.h"

typedef u32 uvec4[4];
typedef u32 uvec2[2];

// --- Private Prototypes ---

#define DBG_MAX (DBG_MAX_WORDS + 1)

void readback_init(ReadBackBuffer *self, M_Resource *rm, VkExtent2D extent) {

  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  u32 tot_pixels = extent.height * extent.width;
//96307200
  RGBufferInfo info = {.capacity = sizeof(u32) * tot_pixels * DBG_MAX,
                       .mem = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       .queue_type = BUFFER_QUEUE_GRAPHIC,
                       .name = "Buffer-Readback",
                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};

  self->buffer = rm_create_buffer(rm, &info);
  self->extent = extent;
  RBuffer *buffer = rm_get_buffer(rm, self->buffer);

  vmaMapMemory(dev->allocator, buffer->alloc, (void **)&self->p_cpu);
  memset(self->p_cpu, 0, tot_pixels * sizeof(u32) * DBG_MAX);
}


ResHandle readback_get_handle(ReadBackBuffer* self){
  return self->buffer;
}

u32 readback_get_push_id(ReadBackBuffer *self, M_Resource *rm) {
  return rm_get_buffer(rm, self->buffer)->bindlessIndex;
}
// return ((ev & 0xFFu) << 24u) | ((key & 0xFFu) << 16u) | ((type & 0xFFu) << 8u) | (len & 0xFFu);
static void decode_record_to_editor(editor_pixel_editor *ed, uint32_t px, uint32_t py, const u32 *data) {
 
u32 record_count = data[0];
u32 data_offset_idx = 1;
for(u32 i = 0; i < record_count; i++){
    u32 header = data[data_offset_idx];
    uint8_t ev = (header >> 24) & 0xFF;
    uint8_t key = (header >> 16) & 0xFF;
    uint8_t type = (header >> 8) & 0xFF;
    uint8_t len = (header >> 0) & 0xFF;

  const char *ev_name;
  switch (ev) {
  case DBG_EV_GENERIC:
    ev_name = "GEN";
    break;
  case DBG_EV_INIT:
    ev_name = "INI";
    break;
  case DBG_EV_BOUNCE:
    ev_name = "BNC";
    break;
  case DBG_EV_LIGHTING:
    ev_name = "LIT";
    break;
  default:
    ev_name = "???";
    break;
  }

  // 2. Resolve Data Key
  const char *key_name;
  switch (key) {
  case DBG_KEY_POS:
    key_name = "Position";
    break;
  case DBG_KEY_NORM:
    key_name = "Normal";
    break;
  case DBG_KEY_ALBEDO:
    key_name = "Albedo";
    break;
  case DBG_KEY_DEPTH:
    key_name = "Depth";
    break;
  default:
    key_name = "Value";
    break;
  }

  // 3. Render Data by Type
  switch (type) {
  case DBG_T_F32: {
    float v = *(float *)&data[i];
    editor_pixel_editor_append_f32(ed, px, py, key_name, v);
    break;
  }
  case DBG_T_VEC3: {
    float *v = (float *)&data[i];
    editor_pixel_editor_append_vec3(ed, px, py, key_name, v);
    break;
  }
  case DBG_T_U32: {
    editor_pixel_editor_append_u32(ed, px, py, key_name, data[i]);
    break;
  }
  case DBG_T_VEC4: {
    NOT_IMPLEMENTED();
    // float *v = (float *)&data[i];
    // editor_pixel_editor_append_u32(ed, px, py, key_name, data[i]);
    // nk_labelf(ctx, NK_TEXT_LEFT, "[%s] %s: %.2f, %.2f, %.2f, %.2f", ev_name, key_name, v[0], v[1], v[2], v[3]);
    break;
  }
  default:
    NOT_IMPLEMENTED();
    break;
  }

  // Advance the index by the reported length
  data_offset_idx += len;
}
}

void readback_write_to_editor(ReadBackBuffer *self, editor_pixel_editor *editor) {
 M_Resource* rm =  SYSTEM_GET(SYSTEM_TYPE_RESOURCE, M_Resource);
  M_GPU* dev =  SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);
  RBuffer* buffer = rm_get_buffer(rm, self->buffer);
  vmaInvalidateAllocation(dev->allocator,buffer->alloc, 0, VK_WHOLE_SIZE);
  for (u32 y = 0; y < self->extent.height; y++) {
    for (u32 x = 0; x < self->extent.width; x++) {
      u32 offset = self->extent.width * y  + x; 
      offset *= DBG_MAX;
      decode_record_to_editor(editor, x, y, &self->p_cpu[offset]);
    }
  }
}
// --- Private Functions ---
