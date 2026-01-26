
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
#define DBG_GET_KEY(h)   ((h >> 16) & 0xFF)
#define DBG_GET_TYPE(h)  ((h >> 8) & 0xFF)
#define DBG_GET_PART(h)  ((h) & 0xFF)
void dbgr_fetch_to_state(M_GPU *gpu, M_Resource *rm, ResHandle buf_handle, uint32_t width, int x, int y, DebugInspectorState *state) {
  uint32_t pixel_index = y * width + x;
  VkDeviceSize offset = (VkDeviceSize)pixel_index * DBG_PIXEL_STRIDE_BYTES;
  VkDeviceSize size = DBG_PIXEL_STRIDE_BYTES;

  RBuffer *r_buf = rm_get_buffer(rm, buf_handle);
  if (!r_buf)
    return;

  // 1. Create Staging Buffer (Host Visible)
  VkBuffer stage_buf;
  VmaAllocation stage_alloc;
  VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VmaAllocationCreateInfo vaci = {.usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
                                  .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT};
  vmaCreateBuffer(gpu->allocator, &bci, &vaci, &stage_buf, &stage_alloc, NULL);

  // 2. Prepare Command Buffer
  // Instead of idling the whole GPU, we just wait for our immediate fence
  // to ensure the command buffer is done with its *previous* job.
  vkWaitForFences(gpu->device, 1, &gpu->imm_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(gpu->device, 1, &gpu->imm_fence);

  VkCommandBuffer cmd = gpu->imm_cmd_buffer;
  vkResetCommandBuffer(cmd, 0); // Reset existing commands

  VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  vkBeginCommandBuffer(cmd, &begin);

  // 3. Barrier 1: Sync Compute Write -> Transfer Read
  // This ensures the GPU has finished writing the pixel data before we try to copy it.
  VkBufferMemoryBarrier b1 = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                              .buffer = r_buf->handle,
                              .offset = offset,
                              .size = size};
  
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &b1, 0,
                       NULL);

  // 4. Copy Buffer
  VkBufferCopy region = {.srcOffset = offset, .dstOffset = 0, .size = size};
  vkCmdCopyBuffer(cmd, r_buf->handle, stage_buf, 1, &region);

  // 5. Barrier 2: Sync Transfer Write -> Host Read
  // This ensures the copy is fully flushed to memory before the CPU reads it.
  VkBufferMemoryBarrier b2 = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                              .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                              .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                              .buffer = stage_buf,
                              .offset = 0,
                              .size = size};
  
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &b2, 0, NULL);

  vkEndCommandBuffer(cmd);

  // 6. Submit & Wait
  VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
  vkQueueSubmit(gpu->graphics_queue, 1, &si, gpu->imm_fence);

  // Wait for THIS specific operation to finish
  vkWaitForFences(gpu->device, 1, &gpu->imm_fence, VK_TRUE, UINT64_MAX);

  // 7. Map & Read Data
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

void debug_ui_layout(DebugInspectorState *state) {
    // Reset the string allocator for this frame (strings must live until render)
    ResetTextArena();

    if (!state->active) return;

    const Clay_Color bg      = { 40,  40,  40, 240 };
    const Clay_Color row_bg  = { 60,  60,  60, 255 };
    const Clay_Color textCol = { 255, 255, 255, 255 };
    const Clay_Color valCol  = { 200, 200, 200, 255 };

    // Clay current style: CLAY(id, { ... })
    CLAY(CLAY_ID("InspectorPanel"), {
        .backgroundColor = bg,
        .cornerRadius = { 8, 8, 8, 8 },
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = {
                .width  = CLAY_SIZING_FIXED(400),
                .height = CLAY_SIZING_FIT(0),
            },
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 8,
        },
    }) {
        // Title Header
        CLAY(CLAY_ID("InspectorHeader"), {
            .layout = {
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .sizing = {
                    .width  = CLAY_SIZING_GROW(0),
                    .height = CLAY_SIZING_FIT(0),
                },
                .childGap = 10,
            },
        }) {
            CLAY_TEXT(
                CLAY_STRING("Pixel Inspector"),
                CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = textCol })
            );

            CLAY_TEXT(
                FormatText("(%d, %d)", state->x, state->y),
                CLAY_TEXT_CONFIG({ .fontSize = 24, .textColor = valCol })
            );
        }

        if (state->count == 0) {
            CLAY_TEXT(
                CLAY_STRING("No events recorded."),
                CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = valCol })
            );
        }

        // Log Rows
        for (uint32_t i = 0; i < state->count; i++) {
            DbgRecord *r = &state->records[i];
            uint32_t type = DBG_GET_TYPE(r->header);

            Clay_String valStr;
            if (type == DBG_T_F32) {
                valStr = FormatText("%.4f", u2f(r->y));
            } else if (type == DBG_T_VEC3) {
                valStr = FormatText("(%.2f, %.2f, ...)", u2f(r->y), u2f(r->z));
            } else {
                valStr = FormatText("%u", r->y);
            }

            CLAY(CLAY_IDI("InspectorRow", i), {
                .backgroundColor = row_bg,
                .cornerRadius = { 4, 4, 4, 4 },
                .layout = {
                    .layoutDirection = CLAY_LEFT_TO_RIGHT,
                    .sizing = {
                        .width  = CLAY_SIZING_GROW(0),
                        .height = CLAY_SIZING_FIT(0),
                    },
                    // padding is { left, right, top, bottom }
                    .padding = { 8, 8, 6, 6 },
                    .childGap = 8,
                },
            }) {
                // Event column
                CLAY(CLAY_IDI("InspectorRow_EventCol", i), {
                    .layout = {
                        .sizing = {
                            .width  = CLAY_SIZING_FIXED(60),
                            .height = CLAY_SIZING_FIT(0),
                        },
                    },
                }) {
                    CLAY_TEXT(
                        FormatText("%u", DBG_GET_EVENT(r->header)),
                        CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = textCol })
                    );
                }

                // Key column
                CLAY(CLAY_IDI("InspectorRow_KeyCol", i), {
                    .layout = {
                        .sizing = {
                            .width  = CLAY_SIZING_FIXED(40),
                            .height = CLAY_SIZING_FIT(0),
                        },
                    },
                }) {
                    CLAY_TEXT(
                        FormatText("%u", DBG_GET_KEY(r->header)),
                        CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = valCol })
                    );
                }

                // Value column (flex)
                CLAY_TEXT(
                    valStr,
                    CLAY_TEXT_CONFIG({ .fontSize = 14, .textColor = textCol })
                );
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
    union { uint32_t u; float f; } c = {u};
    return c.f;
}

// 2. dbgr_cpu_tick

// Goal: Read the data written by the GPU in the previous frame. This must be called at the start of your frame loop, strictly after sm_begin_frame (which waits for the previous frame's fences).

//     Check Validity: Verify that your valid flag is true. If false (first frame), return immediately to avoid reading garbage.

//     Access Data: Cast the stored pMappedData pointer to your data type (e.g., DbgPixelBlock*).

//     Copy Data: Perform a memcpy from this pointer into your UI state struct (DebugInspectorState).

//     No Unmap: Do not unmap or flush. The persistent mapping handles this.

// dbgr_gpu_tick

// Goal: Record commands into the main command buffer to copy the specific pixel data into the readback buffer.

//     Validate Inputs: Check if mouse coordinates are within the source buffer/image bounds.

//     Sync Source Resource: Call the engine's cmd_sync_buffer on the source resource handle.

//         Target State: STATE_TRANSFER

//         Target Access: ACCESS_READ

//         Reason: This updates the M_Resource tracking and ensures previous shaders are done writing to the source.