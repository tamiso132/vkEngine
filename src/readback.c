

#include "rt/rt_shared.glsl"
#include "util.h"

#include "readback.h"
#include <stdio.h>
#include <vk_mem_alloc.h>

// Helper: Interpret uint32 bits as float
static float u2f(uint32_t u) {
    union { uint32_t u; float f; } c;
    c.u = u;
    return c.f;
}

ResHandle dbgr_create_buffer(M_Resource* rm, uint32_t width, uint32_t height) {
    RGBufferInfo info = {
        .name = "RT_Debug_Log",
        .capacity = width * height * DBG_PIXEL_STRIDE_BYTES,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        .queue_type = BUFFER_QUEUE_GRAPHIC
    };
    return rm_create_buffer(rm, &info);
}

void dbgr_analyze_pixel(M_GPU* gpu, M_Resource* rm, ResHandle buf_handle, uint32_t buffer_width, int x, int y) {
    // 1. Calculate specific byte offset for this pixel
    uint32_t pixel_index = y * buffer_width + x;
    VkDeviceSize offset = (VkDeviceSize)pixel_index * DBG_PIXEL_STRIDE_BYTES;
    VkDeviceSize size = DBG_PIXEL_STRIDE_BYTES;

    // 2. Get the internal buffer handle
    RBuffer* r_buf = rm_get_buffer(rm, buf_handle);
    if (!r_buf) {
        printf("[DBGR] Error: Invalid buffer handle.\n");
        return;
    }

    // 3. Create a temporary Staging Buffer (Host Visible)
    VkBuffer stage_buf;
    VmaAllocation stage_alloc;
    VkBufferCreateInfo bci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT
    };
    VmaAllocationCreateInfo vaci = {
        .usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    };
    vmaCreateBuffer(gpu->allocator, &bci, &vaci, &stage_buf, &stage_alloc, NULL);

    // 4. Perform the Copy (Immediate Submit)
    // We assume the compute shader is done writing (WAIT_IDLE or FENCE logic in runner)
    vkQueueWaitIdle(gpu->graphics_queue);

    VkCommandBuffer cmd = gpu->imm_cmd_buffer;
    vkResetFences(gpu->device, 1, &gpu->imm_fence);

    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);

    // Barrier: Storage Buffer Write -> Transfer Read
    VkBufferMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r_buf->handle,
        .offset = offset,
        .size = size
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 1, &barrier, 0, NULL);

    VkBufferCopy region = { .srcOffset = offset, .dstOffset = 0, .size = size };
    vkCmdCopyBuffer(cmd, r_buf->handle, stage_buf, 1, &region);

    // Barrier: Transfer Write -> Host Read
    VkBufferMemoryBarrier host_barrier = barrier;
    host_barrier.buffer = stage_buf;
    host_barrier.offset = 0;
    host_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, NULL, 1, &host_barrier, 0, NULL);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd};
    vkQueueSubmit(gpu->graphics_queue, 1, &si, gpu->imm_fence);
    vkWaitForFences(gpu->device, 1, &gpu->imm_fence, VK_TRUE, 1000000000);

    // 5. Map and Parse
    DbgPixelBlock* data;
    vmaMapMemory(gpu->allocator, stage_alloc, (void**)&data);

    LOG_INFO("\n\033[1;36m=== DEBUG PIXEL LOG [%d, %d] (Entries: %u) ===\033[0m", x, y, data->count);
    
    uint32_t count = data->count;
    if (count > DBG_MAX_RECORDS) count = DBG_MAX_RECORDS;

    if (count == 0) {
        LOG_INFO("  (No events recorded for this pixel)");
    }

    for (uint32_t i = 0; i < count; i++) {
        DbgRecord* r = &data->records[i];
        uint32_t h = r->header;
        
        uint32_t evt  = DBG_GET_EVENT(h);
        uint32_t key  = DBG_GET_KEY(h);
        uint32_t type = DBG_GET_TYPE(h);
        uint32_t part = DBG_GET_PART(h);

        // Fancy printing: Index | Event | Key
        LOG_INFO("  [%02d] \033[1;33mEvt:%-2d Key:%-3d\033[0m ", i, evt, key);

        switch(type) {
            case DBG_T_U32:   LOG_INFO("U32   : %u", r->y); break;
            case DBG_T_I32:   LOG_INFO("I32   : %d", (int)r->y); break;
            case DBG_T_F32:   LOG_INFO("F32   : %.5f", u2f(r->y)); break;
            case DBG_T_VEC2:  LOG_INFO("Vec2  : (%.3f, %.3f)", u2f(r->y), u2f(r->z)); break;
            case DBG_T_IVEC2: LOG_INFO("IVec2 : (%d, %d)", (int)r->y, (int)r->z); break;
            case DBG_T_UVEC2: LOG_INFO("UVec2 : (%u, %u)", r->y, r->z); break;
            
            // Multi-part Vectors (Handle Part 0 vs Part 1)
            case DBG_T_VEC3:
                if (part == 0) LOG_INFO("Vec3  : (%.3f, %.3f, ...)", u2f(r->y), u2f(r->z));
                else           LOG_INFO("Vec3  : (..., ..., %.3f)", u2f(r->y));
                break;
            case DBG_T_VEC4:
                if (part == 0) LOG_INFO("Vec4  : (%.3f, %.3f, ...)", u2f(r->y), u2f(r->z));
                else           LOG_INFO("Vec4  : (..., ..., %.3f, %.3f)", u2f(r->y), u2f(r->z));
                break;
            case DBG_T_IVEC3:
                if (part == 0) LOG_INFO("IVec3 : (%d, %d, ...)", (int)r->y, (int)r->z);
                else           LOG_INFO("IVec3 : (..., ..., %d)", (int)r->y);
                break;

            default: LOG_INFO("Unknown (Type=%u) Raw: %08X %08X %08X", type, r->y, r->z, r->w);
        }
    }
    LOG_INFO("==================================================");

    vmaUnmapMemory(gpu->allocator, stage_alloc);
    vmaDestroyBuffer(gpu->allocator, stage_buf, stage_alloc);
}