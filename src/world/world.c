
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cglm/ivec3.h"
#include "cglm/types.h"
#include "cglm/vec3.h"
#include "command.h"
#include "common.h"
#include "gpu/gpu.h"
#include "resource/resmanager.h"
#include "rt/rt_shared.glsl"
#include "transfer_queue.h"
#include "util.h"
#include "vector.h"

#include "internal.h"

#include "shared_types.h"
#include "world/chunk/chunk_api.h"
#include "world/chunk/chunk_internal.h"

#include "world.h"
// TODO something to implement, replacing indices, that should be uploaded again
// so if an index already exist, just replace

typedef struct ChunkDelta {
  ivec3 dir; // should be -1/0/+1 on a single axis
} ChunkDelta;

typedef struct GPUCtx {
  Vector chunks; // ChunkGPU

  // chunks that need to be uploaded
  ResHandle descriptor_res;
  Vector need_transfer_idx; // u32[], index into active node
  Vector uploaded_idxs;     // u32[], chunks that got uploaded
  bool is_pending;
  u64 submission_signal; // when pending is done
} GPUCtx;

typedef struct GridCtx {
  GridResult grid_result;
  i32 grid_slots[GRID_VOL];               // active node indices
  GPUGridSlot descriptor_slots[GRID_VOL]; // descriptor_slots to upload
} GridCtx;

struct World {
  WorldConfig cfg;

  ivec3 prev_player_chunk;

  Vector chunk_trees; // ChunkTree
  GPUCtx gpu_ctx;
  GridCtx *grid_ctx;

  // GPU storage buffer containing GPUGridSlot[GRID_VOL]

  ivec3 min_corner;

  Vector scratch_indices; // u32[]
  bool is_init;
};

// --- Private Prototypes ---

static void _rebuild_descriptor_buffer(World *w, M_Resource *rm, GPUGridSlot *descriptor_slots);

static void _world_async_upload_gpu(World *w, M_Resource *rm, TransferQueue *transfer);

static void _world_step_grid_init(World *w);

// ------------------------------------------------------------

World *world_create(const WorldConfig *cfg, vec3 player_pos) {
  if (!cfg || cfg->visibility == 0 || cfg->chunk_size == 0)
    return NULL;

  World *w = (World *)calloc(1, sizeof(World));
  w->grid_ctx = calloc(1, sizeof(GridCtx));
  if (!w)
    return NULL;

  w->cfg = *cfg;
  GridCtx *grid_ctx = w->grid_ctx;
  // result vectors
  vec_init(&grid_ctx->grid_result.load_coords, sizeof(ivec3), NULL);
  vec_init(&grid_ctx->grid_result.load_slots, sizeof(u32), NULL);
  vec_init(&grid_ctx->grid_result.unload_coords, sizeof(ivec3), NULL);
  vec_init(&w->scratch_indices, sizeof(u32), NULL);

  vec_init(&w->gpu_ctx.chunks, sizeof(ChunkGPU), NULL);
  vec_init(&w->gpu_ctx.need_transfer_idx, sizeof(u32), NULL);
  vec_init(&w->gpu_ctx.uploaded_idxs, sizeof(u32), NULL);

  vec_init(&w->chunk_trees, sizeof(ChunkTree), NULL);

  ivec3 i32_player_pos;
  vec3_to_ivec3_floor(player_pos, i32_player_pos);
  grid_world_coord_to_chunk_pos(w->min_corner, i32_player_pos, w->prev_player_chunk);

  _world_step_grid_init(w);

  w->is_init = true;

  return w;
}

void world_init_gpu(World *w, M_Resource *rm, TransferQueue *transfer) {
  if (!w)
    return;

  assert(!transfer_in_flight(transfer));

  RGBufferInfo info = {
      .capacity = sizeof(w->grid_ctx->descriptor_slots),
      .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      .queue_type = BUFFER_QUEUE_ALL,
  };

  GPUCtx *gpu_ctx = &w->gpu_ctx;
  GridCtx *grid_ctx = w->grid_ctx;

  w->gpu_ctx.descriptor_res = rm_create_buffer(rm, &info);

  transfer_push_upload(transfer, rm, gpu_ctx->descriptor_res, sizeof(grid_ctx->descriptor_slots),
                       grid_ctx->descriptor_slots, 16);

  _world_async_upload_gpu(w, rm, transfer);
}

void world_destroy(World *w) {
  if (!w)
    return;
  GPUCtx *gpu_ctx = &w->gpu_ctx;
  GridCtx *grid_ctx = w->grid_ctx;
  // TODO, fix later
  //  vec_free(&w->grid_ctx.load_coords);
  //  vec_free(&w->grid_ctx->load_slots);
  //  vec_free(&w->grid_ctx->.unload_coords);
  //
  //  vec_free(&w->desc_updates);
  //
  //  free(w);
}

void world_grid_get_min_corner(World *w, vec3 out_min_corner) {
  if (!w)
    return;

  // TODO, fix later
  //  world-space min corner of the whole grid window
  //  ivec3 origin = {};
  //  glm_ivec3_copy(w->grid.origin_chunk, origin);
  //
  //  vec3 origin_world = {};
  //  _chunk_coord_to_world_pos(&w->cfg, origin, origin_world);
  //  glm_vec3_copy(origin_world, out_min_corner);
}

i32 world_grid_get_push_id(World *w, M_Resource *rm) {
  if (!w)
    abort();

  return (i32)rm_get_buffer_descriptor_index(rm, w->gpu_ctx.descriptor_res);
}

// ------------------------------------------------------------
// GPU TICK
// ------------------------------------------------------------
//
// NOTE: This assumes you changed chunk_gpu_tick() to append DescriptorInfo updates:
//   bool chunk_gpu_tick(ChunkGpu*, M_Resource*, TransferQueue*, Vector* out_desc_updates);
//
// If you haven't yet, either do that change OR remove &w->desc_updates in the call.
//
void world_gpu_tick(World *w, CmdBuffer main_cmd, M_Resource *rm, TransferQueue *transfer) {
  if (!w)
    return;

  if (transfer_in_flight(transfer))
    return;

  M_GPU *dev = SYSTEM_GET(SYSTEM_TYPE_GPU, M_GPU);

  u32 current_ticket = transfer_get_current_ticket_completed(transfer);
  assert(current_ticket >= w->gpu_ctx.submission_signal);

  //  check uploaded list, then swap all of them.
  if (w->gpu_ctx.is_pending) {
    ChunkGPUInput swap_input = {.chunk = w->gpu_ctx.chunks, .indices = w->gpu_ctx.uploaded_idxs};
    LOG_INFO_TAG("World", "Swap buffers count: %ld", swap_input.indices.length);
    chunk_gpu_swap_buffers(swap_input, rm);

    GridInput grid_input = {.slots = w->grid_ctx->grid_slots};
    glm_ivec3_copy(w->min_corner, grid_input.min_corner);

    if (w->gpu_ctx.uploaded_idxs.length > 0) {
      LOG_INFO_TAG("World", "Rebuild Descriptor Buffer");
      _rebuild_descriptor_buffer(w, rm, w->grid_ctx->descriptor_slots);
      cmd_buffer_upload(main_cmd, dev, rm, w->gpu_ctx.descriptor_res, w->grid_ctx->descriptor_slots,
                        sizeof(w->grid_ctx->descriptor_slots));
    }

    w->gpu_ctx.is_pending = false;
    vec_clear(&w->gpu_ctx.uploaded_idxs);
  }

  assert(w->gpu_ctx.uploaded_idxs.length == 0);

  // async transfer if needed
  if (w->gpu_ctx.need_transfer_idx.length > 0) {
    _world_async_upload_gpu(w, rm, transfer);
  }
}

// ------------------------------------------------------------
// CPU TICK
// ------------------------------------------------------------
void world_cpu_tick(World *w, vec3 player_pos) {
  if (!w)
    return;

  // TODO, do grid_step
  // TODO, Add any need load chunks to transfer list
  // TODO, if grid updated, then also set it dirty

  ivec3 i32_player_pos;
  vec3_to_ivec3_floor(player_pos, i32_player_pos);

  ivec3 player_chunk = {};
  grid_world_coord_to_chunk_pos(w->min_corner, i32_player_pos, player_chunk);

  ivec3 delta_chunk = {};
  glm_ivec3_sub(w->prev_player_chunk, player_chunk, delta_chunk);

  if (glm_ivec3_dot(delta_chunk, delta_chunk) == 0) {
    return;
  }

  vec_clear(&w->grid_ctx->grid_result.load_coords);
  vec_clear(&w->grid_ctx->grid_result.unload_coords);
  vec_clear(&w->grid_ctx->grid_result.load_slots);
  GridInput grid_input = {.slots = w->grid_ctx->grid_slots};

  glm_ivec3_copy(w->min_corner, grid_input.min_corner);
  grid_step(grid_input, delta_chunk, &w->grid_ctx->grid_result);
}

// ------------------------------------------------------------
// Private helpers
// ------------------------------------------------------------

// --- Private Functions ---

static void _rebuild_descriptor_buffer(World *w, M_Resource *rm, GPUGridSlot *descriptor_slots) {
  i32 active_order[GRID_VOL];
  GridInput grid_input = {.slots = w->grid_ctx->grid_slots};

  for (u32 z = 0; z < MAX_CHUNK_VISIBILITY; z++) {
    for (u32 y = 0; y < MAX_CHUNK_VISIBILITY; y++) {
      for (u32 x = 0; x < MAX_CHUNK_VISIBILITY; x++) {
        GridSlot slot = {x, y, z};
        u32 slot_idx = grid_get_chunk_index(grid_input, slot);
        active_order[slot_idx] = w->grid_ctx->grid_slots[slot_idx];
      }
    }
  }
  ChunkGPUInput desc_input = {.chunk = w->gpu_ctx.chunks, .indices = active_order};
  chunk_gpu_get_descriptor_indices(desc_input, rm, descriptor_slots);
}

static void _world_async_upload_gpu(World *w, M_Resource *rm, TransferQueue *transfer) {
  GPUCtx *gpu_ctx = &w->gpu_ctx;
  GridCtx *grid_ctx = w->grid_ctx;

  // TODO, remove later and have another solution
  vec_append_zero(&w->gpu_ctx.chunks, w->gpu_ctx.need_transfer_idx.length);

  ChunkGPUInput gpu_input = {.chunk = w->gpu_ctx.chunks, .indices = gpu_ctx->need_transfer_idx};
  ChunkInput chunk_input_view = {.trees = w->chunk_trees, .indices = gpu_ctx->need_transfer_idx};

  // TODO, maybe memset
  ChunkUploadView views[gpu_ctx->need_transfer_idx.length];
  chunk_get_upload_view(chunk_input_view, views);

  LOG_INFO_TAG("World", "Load %ld chunks",gpu_ctx->need_transfer_idx.length);
  chunk_gpu_init(gpu_input, rm, transfer, views);

  // ok to upload zeros; first world_gpu_tick will rebuild real values

  // move the indices to uploaded list
  // TODO, continue if same indices already exist
  // maybe use a bitmask, to chec
  
  vec_copy(&gpu_ctx->need_transfer_idx, &gpu_ctx->uploaded_idxs);
  vec_clear(&gpu_ctx->need_transfer_idx);

  w->gpu_ctx.is_pending = true;
}

static void _world_step_grid_init(World *w) {
  GridCtx *grid_ctx = w->grid_ctx;

  GridInput grid_input = (GridInput){.slots = grid_ctx->grid_slots};
  memcpy(grid_input.min_corner, w->min_corner, sizeof(vec3));

  grid_init(&grid_input, w->prev_player_chunk, &grid_ctx->grid_result);

  //   CHUNKS THAT NEED TO BE LOADED IN
  vec_clear(&w->scratch_indices);

  u32 old_len = vec_append_zero(&w->chunk_trees, grid_ctx->grid_result.load_slots.length);

  for (u32 i = 0; i < grid_ctx->grid_result.load_slots.length; i++) {
    u32 active_idx = old_len + i;
    vec_push(&w->scratch_indices, &active_idx);
  }

  ChunkInput chunk_input = {};
  chunk_input.indices = w->scratch_indices;
  chunk_input.trees = w->chunk_trees;

  chunk_init(chunk_input, grid_ctx->grid_result.load_coords);

  vec_append(&w->scratch_indices, &w->gpu_ctx.need_transfer_idx);
}
