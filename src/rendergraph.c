#include "rendergraph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Configuration ---
#define RG_INIT_CAP 8
#define MAX_RESOURCES 1024 // Must match resmanager's limit

// --- Internal Helper: Access Flag Deduction ---
// This mimics the 'getAccessFlags' logic from your C++ code.
// It guesses the correct VkAccessFlagBits2 based on the pipeline stage and
// usage type.
static VkAccessFlags2 _deduce_access_flags(VkPipelineStageFlags2 stage,
                                           RGUsageType type) {
  bool is_write = (type & RG_USAGE_WRITE);

  // 1. Color Attachment
  if (stage & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) {
    if (is_write)
      return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
  }

  // 2. Depth Stencil
  if (stage & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
               VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) {
    if (is_write)
      return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  }

  // 3. Shaders (Vertex, Frag, Compute)
  if (stage & (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
               VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)) {
    if (is_write)
      return VK_ACCESS_2_SHADER_WRITE_BIT;
    return VK_ACCESS_2_SHADER_READ_BIT;
  }

  // 4. Transfer
  if (stage & VK_PIPELINE_STAGE_2_TRANSFER_BIT) {
    if (is_write)
      return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    return VK_ACCESS_2_TRANSFER_READ_BIT;
  }

  // 5. Host
  if (stage & VK_PIPELINE_STAGE_2_HOST_BIT) {
    if (is_write)
      return VK_ACCESS_2_HOST_WRITE_BIT;
    return VK_ACCESS_2_HOST_READ_BIT;
  }

  // Fallback/General
  if (is_write)
    return VK_ACCESS_2_MEMORY_WRITE_BIT;
  return VK_ACCESS_2_MEMORY_READ_BIT;
}

// --- Lifecycle ---

RenderGraph *rg_init(ResourceManager *rm) {
  RenderGraph *rg = malloc(sizeof(RenderGraph));
  rg->rm = rm;

  rg->pass_count = 0;
  rg->pass_cap = RG_INIT_CAP;
  rg->passes = malloc(sizeof(RGPass) * rg->pass_cap);

  // Allocate state tracking for every possible resource ID
  rg->resource_states = malloc(sizeof(RGSyncState) * MAX_RESOURCES);

  return rg;
}

void rg_destroy(RenderGraph *rg) {
  for (uint32_t i = 0; i < rg->pass_count; i++) {
    RGPass *p = &rg->passes[i];
    if (p->usages)
      free(p->usages);
    if (p->img_barriers)
      free(p->img_barriers);
    if (p->buf_barriers)
      free(p->buf_barriers);
  }
  free(rg->passes);
  free(rg->resource_states);
  free(rg);
}

// --- Pass Building ---

RGPass *rg_add_pass(RenderGraph *rg, const char *name, RGTaskType type,
                    RGPassFunc func, void *user_data) {
  if (rg->pass_count >= rg->pass_cap) {
    rg->pass_cap *= 2;
    rg->passes = realloc(rg->passes, sizeof(RGPass) * rg->pass_cap);
  }

  RGPass *p = &rg->passes[rg->pass_count++];
  strncpy(p->name, name, 63);
  p->type = type;
  p->func = func;
  p->user_data = user_data;

  // Init Usages
  p->usage_count = 0;
  p->usage_cap = RG_INIT_CAP;
  p->usages = malloc(sizeof(RGResourceUsage) * p->usage_cap);

  // Init Barriers (Empty initially)
  p->img_barriers = NULL;
  p->img_barrier_count = 0;
  p->buf_barriers = NULL;
  p->buf_barrier_count = 0;

  return p;
}

// Internal helper to add usage to a pass
static void _rg_add_usage(RGPass *p, RGHandle res, RGUsageType type,
                          VkPipelineStageFlags2 stage, VkImageLayout layout) {
  if (p->usage_count >= p->usage_cap) {
    p->usage_cap *= 2;
    p->usages = realloc(p->usages, sizeof(RGResourceUsage) * p->usage_cap);
  }

  RGResourceUsage *u = &p->usages[p->usage_count++];
  u->handle = res;
  u->type = type;
  u->stage = stage;
  u->layout = layout;
  u->access = _deduce_access_flags(stage, type);
}

void rg_pass_read(RGPass *pass, RGHandle resource,
                  VkPipelineStageFlags2 stage) {
  _rg_add_usage(pass, resource, RG_USAGE_READ, stage,
                VK_IMAGE_LAYOUT_UNDEFINED);
}

void rg_pass_write(RGPass *pass, RGHandle resource,
                   VkPipelineStageFlags2 stage) {
  _rg_add_usage(pass, resource, RG_USAGE_WRITE, stage,
                VK_IMAGE_LAYOUT_UNDEFINED);
}

void rg_pass_set_color_target(RGPass *pass, RGHandle image,
                              VkClearValue clear) {
  // Implies: Write access, Color Output Stage, Optimal Layout
  _rg_add_usage(pass, image, RG_USAGE_WRITE,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void rg_pass_texture_read(RGPass *pass, RGHandle image) {
  // Implies: Read access, Fragment Shader Stage, Read-Only Layout
  _rg_add_usage(pass, image, RG_USAGE_READ,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// --- Compilation (Barrier Generation) ---

void rg_compile(RenderGraph *rg) {
  // 1. Reset Global State
  //    This simulates the state of resources at the very beginning of the frame
  //    (before Pass 0).
  for (uint32_t i = 0; i < MAX_RESOURCES; i++) {
    RGResource *res = &rg->rm->resources[i];
    RGSyncState *state = &rg->resource_states[i];

    state->last_stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    state->last_access = VK_ACCESS_2_NONE;
    state->is_written = false;

    // Important: If it's an imported image (Swapchain), use its current real
    // layout. If it's internal, assume Undefined.
    if (res->type == RES_TYPE_IMAGE) {
      state->last_layout = res->img.current_layout;
    }
  }

  // 2. Iterate Passes linearly
  for (uint32_t p_idx = 0; p_idx < rg->pass_count; p_idx++) {
    RGPass *pass = &rg->passes[p_idx];

    // Re-allocate barriers for this frame's compilation
    // (Freeing old ones if they exist to support re-compilation)
    if (pass->img_barriers)
      free(pass->img_barriers);
    if (pass->buf_barriers)
      free(pass->buf_barriers);

    // Allocate max possible barriers (1 per usage is safe upper bound)
    pass->img_barriers =
        malloc(sizeof(VkImageMemoryBarrier2) * pass->usage_count);
    pass->buf_barriers =
        malloc(sizeof(VkBufferMemoryBarrier2) * pass->usage_count);
    pass->img_barrier_count = 0;
    pass->buf_barrier_count = 0;

    // 3. Check Resources used in this pass against Global State
    for (uint32_t u_idx = 0; u_idx < pass->usage_count; u_idx++) {
      RGResourceUsage *usage = &pass->usages[u_idx];
      uint32_t id = usage->handle.id;
      RGResource *res = &rg->rm->resources[id];
      RGSyncState *state = &rg->resource_states[id];

      bool is_write = (usage->type & RG_USAGE_WRITE);
      bool is_image = (res->type == RES_TYPE_IMAGE);

      // Decision: Do we need a barrier?
      bool need_barrier = false;

      // Hazard Check:
      // - If previously written, we ALWAYS need a barrier (Write->Read or
      // Write->Write).
      // - If previously read, and we are now Writing, we need a barrier
      // (Read->Write).
      // - (Read->Read does not need a barrier).
      if (state->is_written || is_write) {
        // However, the very first usage might not need a barrier if it's
        // implicitly handled, BUT we usually emit one anyway to transition from
        // TOP_OF_PIPE or acquire layout.
        need_barrier = true;
      }

      // Layout Transition Check (Images only):
      if (is_image) {
        if (usage->layout != VK_IMAGE_LAYOUT_UNDEFINED &&
            usage->layout != state->last_layout) {
          need_barrier = true;
        }
      }

      // Optimization: If nothing happened (TOP_OF_PIPE) and we just read
      // without layout change, maybe skip? But for safety in this
      // implementation, we barrier if the state is "dirty" or layout differs.

      if (need_barrier) {
        if (is_image) {
          VkImageMemoryBarrier2 *b =
              &pass->img_barriers[pass->img_barrier_count++];
          b->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
          b->pNext = NULL;
          b->srcStageMask = state->last_stage;
          b->srcAccessMask = state->last_access;
          b->dstStageMask = usage->stage;
          b->dstAccessMask = usage->access;
          b->oldLayout = state->last_layout;
          b->newLayout = usage->layout;

          // Queue Family Ignored for now (assuming single queue)
          b->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b->image = res->img.img.vk;

          // Default to whole image for now
          b->subresourceRange =
              (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        } else {
          VkBufferMemoryBarrier2 *b =
              &pass->buf_barriers[pass->buf_barrier_count++];
          b->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
          b->pNext = NULL;
          b->srcStageMask = state->last_stage;
          b->srcAccessMask = state->last_access;
          b->dstStageMask = usage->stage;
          b->dstAccessMask = usage->access;
          b->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
          b->buffer = res->buf.vk;
          b->offset = 0;
          b->size = VK_WHOLE_SIZE;
        }
      }

      // 4. Update Global State
      state->last_stage = usage->stage;
      state->last_access = usage->access;

      // Track "Written" state.
      // If we write, is_written = true.
      // If we read, we *keep* the previous is_written state?
      // No, standard practice: if we Read-After-Write, the hazard is resolved.
      // But if we Read-After-Read, the previous Write is still the hazard
      // origin for the NEXT write. Simplified logic: If we modify it, mark
      // written. If we just read, keep the flag? Actually, once we barrier for
      // Read, the memory is visible. The next barrier needs to know if the
      // *previous op* was a write.
      state->is_written = is_write;

      if (is_image && usage->layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        state->last_layout = usage->layout;
      }
    }
  }

  // 5. Update Resource Manager State
  // Persist the final layouts back to the RM so the next frame knows where we
  // left off.
  for (uint32_t i = 0; i < MAX_RESOURCES; i++) {
    RGResource *res = &rg->rm->resources[i];
    if (res->type == RES_TYPE_IMAGE) {
      res->img.current_layout = rg->resource_states[i].last_layout;
    }
  }
}

// --- Execution ---

void rg_execute(RenderGraph *rg, VkCommandBuffer cmd) {
  for (uint32_t i = 0; i < rg->pass_count; i++) {
    RGPass *p = &rg->passes[i];

    // 1. Submit Barriers
    if (p->img_barrier_count > 0 || p->buf_barrier_count > 0) {
      VkDependencyInfo dep = {
          .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
          .imageMemoryBarrierCount = p->img_barrier_count,
          .pImageMemoryBarriers = p->img_barriers,
          .bufferMemoryBarrierCount = p->buf_barrier_count,
          .pBufferMemoryBarriers = p->buf_barriers,
      };
      vkCmdPipelineBarrier2(cmd, &dep);
    }

    // 2. Execute User Callback
    if (p->func) {
      p->func(cmd, p->user_data);
    }
  }
}