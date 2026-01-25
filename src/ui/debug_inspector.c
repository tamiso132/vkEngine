#include "ui/debug_inspector.h"
#include <clay.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vk_mem_alloc.h>

// --- Helper: Frame-Based String Allocator ---
// We need this because Clay stores pointers to strings, not copies.
// Dynamic strings must persist until the Render phase is finished.
static char s_TextArena[8192];
static size_t s_TextCursor = 0;

// --- Private Prototypes ---
static Clay_String FormatText(const char *fmt, ...);

static void ResetTextArena();

static float u2f(uint32_t u);

// --- Macros to unpack debug headers ---
#define DBG_GET_EVENT(h) ((h >> 24) & 0xFF)
#define DBG_GET_KEY(h) ((h >> 16) & 0xFF)
#define DBG_GET_TYPE(h) ((h >> 8) & 0xFF)
#define DBG_GET_PART(h) ((h) & 0xFF)

// --- Fetch Data Logic ---
void dbgr_fetch_to_state(M_GPU *gpu, M_Resource *rm, ResHandle buf_handle, uint32_t width, int x, int y,
                         DebugInspectorState *state) {
  uint32_t pixel_index = y * width + x;
  VkDeviceSize offset = (VkDeviceSize)pixel_index * DBG_PIXEL_STRIDE_BYTES;
  VkDeviceSize size = DBG_PIXEL_STRIDE_BYTES;

  RBuffer *r_buf = rm_get_buffer(rm, buf_handle);
  if (!r_buf)
    return;

  // Create Staging
  VkBuffer stage_buf;
  VmaAllocation stage_alloc;
  VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VmaAllocationCreateInfo vaci = {.usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT};
  vmaCreateBuffer(gpu->allocator, &bci, &vaci, &stage_buf, &stage_alloc, NULL);

  // Command Buffer (Immediate)
  vkQueueWaitIdle(gpu->graphics_queue);
  VkCommandBuffer cmd = gpu->imm_cmd_buffer;
  vkResetFences(gpu->device, 1, &gpu->imm_fence);

  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cmd, &begin);

  // Barrier: Shader -> Transfer
  VkBufferMemoryBarrier b1 = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                              .buffer = r_buf->handle,
                              .offset = offset,
                              .size = size};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &b1, 0,
                       NULL);

  VkBufferCopy region = {.srcOffset = offset, .dstOffset = 0, .size = size};
  vkCmdCopyBuffer(cmd, r_buf->handle, stage_buf, 1, &region);

  // Barrier: Transfer -> Host
  VkBufferMemoryBarrier b2 = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                              .buffer = stage_buf,
                              .offset = 0,
                              .size = size};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &b2, 0, NULL);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
  vkQueueSubmit(gpu->graphics_queue, 1, &si, gpu->imm_fence);
  vkWaitForFences(gpu->device, 1, &gpu->imm_fence, VK_TRUE, 1000000000);

  // Map & Copy
  DbgPixelBlock *data;
  vmaMapMemory(gpu->allocator, stage_alloc, (void **)&data);

  state->active = true;
  state->x = x;
  state->y = y;
  state->count = (data->count > DBG_MAX_RECORDS) ? DBG_MAX_RECORDS : data->count;
  memcpy(state->records, data->records, sizeof(DbgRecord) * state->count);

  vmaUnmapMemory(gpu->allocator, stage_alloc);
  vmaDestroyBuffer(gpu->allocator, stage_buf, stage_alloc);
}

// --- Layout Logic ---
void debug_ui_layout(DebugInspectorState *state) {
  // Reset the string allocator for this frame
  ResetTextArena();

  if (!state->active)
    return;

  Clay_Color bg = {40, 40, 40, 240};
  Clay_Color row_bg = {60, 60, 60, 255};
  Clay_Color text_col = {255, 255, 255, 255};
  Clay_Color val_col = {200, 200, 200, 255};

  CLAY({.id = CLAY_ID("InspectorPanel"),
        .layout = {.sizing = {.width = CLAY_SIZING_FIXED(400)},
                   .layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .padding = {16, 16},
                   .childGap = 8},
        .backgroundColor = bg,
        .cornerRadius = 8}) {
    // Title Header
    CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT, .childGap = 10}}) {
      CLAY_TEXT(CLAY_STRING("Pixel Inspector"), CLAY_TEXT_CONFIG({.fontSize = 24, .textColor = text_col}));

      // Use FormatText instead of CLAY_STRING(buf)
      CLAY_TEXT(FormatText("(%d, %d)", state->x, state->y), CLAY_TEXT_CONFIG({.fontSize = 24, .textColor = val_col}));
    }

    if (state->count == 0) {
      CLAY_TEXT(CLAY_STRING("No events recorded."), CLAY_TEXT_CONFIG({.fontSize = 16, .textColor = val_col}));
    }

    // Log Rows
    for (uint32_t i = 0; i < state->count; i++) {
      DbgRecord *r = &state->records[i];
      uint32_t type = DBG_GET_TYPE(r->header);

      // Format Value
      Clay_String valStr;
      if (type == DBG_T_F32)
        valStr = FormatText("%.4f", u2f(r->y));
      else if (type == DBG_T_VEC3)
        valStr = FormatText("(%.2f, %.2f, ...)", u2f(r->y), u2f(r->z));
      else
        valStr = FormatText("%u", r->y);

      CLAY({.layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT,
                       .padding = {8, 6},
                       .sizing = {.width = CLAY_SIZING_GROW(0)}},
            .backgroundColor = row_bg,
            .cornerRadius = 4}) {
        // ID
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(30)}}}) {
          CLAY_TEXT(FormatText("#%d", i), CLAY_TEXT_CONFIG({.fontSize = 14, .textColor = val_col}));
        }
        // Event
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(60)}}}) {
          CLAY_TEXT(FormatText("%d", DBG_GET_EVENT(r->header)),
                    CLAY_TEXT_CONFIG({.fontSize = 14, .textColor = text_col}));
        }
        // Key
        CLAY({.layout = {.sizing = {.width = CLAY_SIZING_FIXED(40)}}}) {
          CLAY_TEXT(FormatText("%d", DBG_GET_KEY(r->header)), CLAY_TEXT_CONFIG({.fontSize = 14, .textColor = val_col}));
        }
        // Value
        CLAY_TEXT(valStr, CLAY_TEXT_CONFIG({.fontSize = 14, .textColor = text_col}));
      }
    }
  }
}
// --- Private Functions ---

static Clay_String FormatText(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);

  // Check remaining space
  if (s_TextCursor >= sizeof(s_TextArena)) {
    va_end(args);
    return (Clay_String){.length = 0, .chars = ""};
  }

  char *ptr = &s_TextArena[s_TextCursor];
  size_t remaining = sizeof(s_TextArena) - s_TextCursor;

  int len = vsnprintf(ptr, remaining, fmt, args);
  va_end(args);

  if (len < 0 || (size_t)len >= remaining) {
    // Truncated or error
    len = (int)remaining - 1;
    ptr[len] = '\0';
  }

  s_TextCursor += (len + 1); // Advance cursor

  return (Clay_String){.length = len, .chars = ptr};
}

static void ResetTextArena() { s_TextCursor = 0; }

// --- Type Helpers ---
static float u2f(uint32_t u) {
  union {
    uint32_t u;
    float f;
  } c = {u};
  return c.f;
}
