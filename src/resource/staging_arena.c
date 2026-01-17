
#include "staging_arena.h"
#include <assert.h>
// -----------------------------
// Internal buffer bookkeeping
// -----------------------------
typedef struct SgrBuffer {
  VkBuffer buffer;
  VmaAllocation alloc;
  void *mapped;
  VkDeviceSize capacity;

  VkDeviceSize head;
  VkDeviceSize tail;

  bool is_coherent;

  VkDeviceSize *frame_end; // length = frames_in_flight
  u32 frames_in_flight;
} SgrBuffer;

typedef struct StagingGrowRing {
  VmaAllocator vma;
  u32 frames_in_flight;

  SgrBuffer *active;

  // retiring buffers (older ones), destroyed when empty
  Vector retiring; // element type: SgrBuffer*

  VkDeviceSize min_grow_capacity;  // e.g. initial cap
  VkDeviceSize max_total_capacity; // 0 = no cap
  VkDeviceSize total_capacity;     // sum of active + retiring capacities

  bool allow_grow;
} StagingGrowRing;

// --- Private Prototypes ---
static inline VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a);

static StagingSlice buffer_alloc(SgrBuffer *b, VkDeviceSize size, VkDeviceSize alignment);
static void buffer_destroy(VmaAllocator vma, SgrBuffer *b);
static void buffer_flush_slot(SgrBuffer *b, u32 slot);
static bool buffer_init(SgrBuffer **out, VmaAllocator vma, VkDeviceSize capacity, u32 frames_in_flight);
static bool buffer_is_empty(const SgrBuffer *b);
static void buffer_on_new_frame(SgrBuffer *b, u32 slot);

static VkDeviceSize next_capacity(VkDeviceSize cur, VkDeviceSize need_min);

static VkDeviceSize ring_free(const SgrBuffer *b);
static VkDeviceSize ring_used(const SgrBuffer *b);

static bool sgr_grow(StagingGrowRing *sgr, VkDeviceSize need_min);

// -----------------------------
// Public API
// -----------------------------
StagingGrowRing *sgr_init(VmaAllocator vma, VkDeviceSize initial_capacity, u32 frames_in_flight, bool allow_grow) {
  StagingGrowRing *sgr = calloc(sizeof(StagingGrowRing), 1);
  memset(sgr, 0, sizeof(*sgr));
  sgr->vma = vma;
  sgr->frames_in_flight = frames_in_flight;
  sgr->allow_grow = allow_grow;
  sgr->min_grow_capacity = initial_capacity;
  sgr->max_total_capacity = 0;
  sgr->total_capacity = 0;

  // retiring list (SgrBuffer*)
  vec_init(&sgr->retiring, sizeof(SgrBuffer *), NULL);

  if (!buffer_init(&sgr->active, vma, initial_capacity, frames_in_flight)) {
    vec_destroy(&sgr->retiring);
    return NULL;
  }

  sgr->total_capacity = initial_capacity;
  return sgr;
}

void sgr_destroy(StagingGrowRing *sgr) {
  if (!sgr)
    return;

  buffer_destroy(sgr->vma, sgr->active);
  sgr->active = NULL;

  // destroy retiring
  for (u32 i = 0; i < (u32)vec_len(&sgr->retiring); ++i) {
    SgrBuffer *b = *VEC_AT(&sgr->retiring, i, SgrBuffer *);
    buffer_destroy(sgr->vma, b);
  }
  vec_destroy(&sgr->retiring);

  memset(sgr, 0, sizeof(*sgr));
}

void sgr_set_max_total_capacity(StagingGrowRing *sgr, VkDeviceSize max_total) { sgr->max_total_capacity = max_total; }

void sgr_on_new_frame(StagingGrowRing *sgr, u32 slot) {
  assert(slot < sgr->frames_in_flight);

  // Apply release to active
  buffer_on_new_frame(sgr->active, slot);

  // Apply release to retiring buffers
  for (u32 i = 0; i < (u32)vec_len(&sgr->retiring); ++i) {
    SgrBuffer *b = *VEC_AT(&sgr->retiring, i, SgrBuffer *);
    buffer_on_new_frame(b, slot);
  }

  // Remove/destroy retiring buffers that became empty
  // Iterate backwards so remove_at is safe
  for (int i = (int)vec_len(&sgr->retiring) - 1; i >= 0; --i) {
    SgrBuffer *b = *VEC_AT(&sgr->retiring, (u32)i, SgrBuffer *);
    if (buffer_is_empty(b)) {
      sgr->total_capacity -= b->capacity;
      buffer_destroy(sgr->vma, b);
      vec_remove_at(&sgr->retiring, (u32)i);
    }
  }
}

void sgr_flush(StagingGrowRing *sgr, u32 slot) {
  assert(slot < sgr->frames_in_flight);
  buffer_flush_slot(sgr->active, slot);
}

StagingSlice sgr_alloc(StagingGrowRing *sgr, VkDeviceSize size, VkDeviceSize alignment) {
  StagingSlice s = buffer_alloc(sgr->active, size, alignment);
  if (s.cpu_ptr)
    return s;

  VkDeviceSize need_min = size + (alignment ? alignment : 16);
  if (!sgr_grow(sgr, need_min)) {
    return (StagingSlice){0};
  }

  return buffer_alloc(sgr->active, size, alignment);
}

void sgr_flush_range(StagingGrowRing *sgr, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
  if (size == 0)
    return;

  SgrBuffer *owner = NULL;
  if (sgr->active && sgr->active->buffer == buffer) {
    owner = sgr->active;
  } else {
    for (u32 i = 0; i < (u32)vec_len(&sgr->retiring); ++i) {
      SgrBuffer *b = *VEC_AT(&sgr->retiring, i, SgrBuffer *);
      if (b->buffer == buffer) {
        owner = b;
        break;
      }
    }
  }

  assert(owner && "sgr_flush_range: buffer not owned by staging system");
  if (!owner)
    return;

  if (!owner->is_coherent) {
    vmaFlushAllocation(sgr->vma, owner->alloc, offset, size);
  }
}

VkDeviceSize sgr_active_used_bytes(const StagingGrowRing *sgr) { return ring_used(sgr->active); }

VkDeviceSize sgr_active_free_bytes(const StagingGrowRing *sgr) { return ring_free(sgr->active); }

// --- Private Functions ---

static inline VkDeviceSize align_up(VkDeviceSize v, VkDeviceSize a) {
  if (a == 0)
    return v;
  return (v + (a - 1)) & ~(a - 1);
}

// Contiguous-only alloc on one buffer
static StagingSlice buffer_alloc(SgrBuffer *b, VkDeviceSize size, VkDeviceSize alignment) {
  if (size == 0 || size > b->capacity)
    return (StagingSlice){0};
  alignment = alignment ? alignment : 16;

  // Case A: head >= tail, free = [head..cap) + [0..tail)
  if (b->head >= b->tail) {
    VkDeviceSize off = align_up(b->head, alignment);
    VkDeviceSize end = off + size;

    // Try end region
    if (end <= b->capacity) {
      b->head = (end == b->capacity) ? 0 : end;
      return (StagingSlice){
          .buffer = b->buffer,
          .offset = off,
          .size = size,
          .cpu_ptr = (uint8_t *)b->mapped + off,
      };
    }

    // Wrap to start
    VkDeviceSize off2 = align_up((VkDeviceSize)0, alignment);
    VkDeviceSize end2 = off2 + size;
    if (end2 <= b->tail) {
      b->head = end2;
      return (StagingSlice){
          .buffer = b->buffer,
          .offset = off2,
          .size = size,
          .cpu_ptr = (uint8_t *)b->mapped + off2,
      };
    }

    return (StagingSlice){0};
  }

  // Case B: head < tail, free = [head..tail)
  {
    VkDeviceSize off = align_up(b->head, alignment);
    VkDeviceSize end = off + size;
    if (end <= b->tail) {
      b->head = end;
      return (StagingSlice){
          .buffer = b->buffer,
          .offset = off,
          .size = size,
          .cpu_ptr = (uint8_t *)b->mapped + off,
      };
    }
    return (StagingSlice){0};
  }
}

static void buffer_destroy(VmaAllocator vma, SgrBuffer *b) {
  if (!b)
    return;
  if (b->buffer)
    vmaDestroyBuffer(vma, b->buffer, b->alloc);
  free(b->frame_end);
  free(b);
}

// Record coarse ownership end: frame_end[slot] = head
static void buffer_flush_slot(SgrBuffer *b, u32 slot) {
  assert(slot < b->frames_in_flight);
  b->frame_end[slot] = b->head;
}

static bool buffer_init(SgrBuffer **out, VmaAllocator vma, VkDeviceSize capacity, u32 frames_in_flight) {
  SgrBuffer *b = (SgrBuffer *)calloc(1, sizeof(SgrBuffer));
  if (!b)
    return false;

  b->frames_in_flight = frames_in_flight;
  b->frame_end = (VkDeviceSize *)calloc(frames_in_flight, sizeof(VkDeviceSize));
  if (!b->frame_end) {
    free(b);
    return false;
  }

  VkBufferCreateInfo bi = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = capacity,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo ai = {0};
  ai.usage = VMA_MEMORY_USAGE_AUTO;
  ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VmaAllocationInfo outInfo = {0};
  VkResult res = vmaCreateBuffer(vma, &bi, &ai, &b->buffer, &b->alloc, &outInfo);
  if (res != VK_SUCCESS) {
    free(b->frame_end);
    free(b);
    return false;
  }

  b->mapped = outInfo.pMappedData;
  b->capacity = capacity;
  b->head = 0;
  b->tail = 0;

  VkMemoryPropertyFlags memFlags = 0;
  vmaGetAllocationMemoryProperties(vma, b->alloc, &memFlags);
  b->is_coherent = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

  for (u32 i = 0; i < frames_in_flight; ++i)
    b->frame_end[i] = 0;

  *out = b;
  return true;
}

// Destroyability for dumb per-slot release:
// - ring appears empty (head==tail)
// - and every slot's frame_end equals tail (meaning no slot is "holding" a different tail target)
static bool buffer_is_empty(const SgrBuffer *b) {
  if (b->head != b->tail)
    return false;
  for (u32 i = 0; i < b->frames_in_flight; ++i) {
    if (b->frame_end[i] != b->tail)
      return false;
  }
  return true;
}

// Apply coarse release for a slot: tail = frame_end[slot]
static void buffer_on_new_frame(SgrBuffer *b, u32 slot) {
  assert(slot < b->frames_in_flight);
  b->tail = b->frame_end[slot];
  if (b->tail >= b->capacity)
    b->tail %= b->capacity;
}

static VkDeviceSize next_capacity(VkDeviceSize cur, VkDeviceSize need_min) {
  VkDeviceSize cap = cur ? cur : (VkDeviceSize)1;
  while (cap < need_min)
    cap *= 2;
  return cap;
}

static VkDeviceSize ring_free(const SgrBuffer *b) {
  VkDeviceSize used = ring_used(b);
  if (used >= b->capacity)
    return 0;
  return b->capacity - used;
}

static VkDeviceSize ring_used(const SgrBuffer *b) {
  if (b->head == b->tail)
    return 0;
  if (b->head > b->tail)
    return b->head - b->tail;
  return (b->capacity - b->tail) + b->head;
}

static bool sgr_grow(StagingGrowRing *sgr, VkDeviceSize need_min) {
  if (!sgr->allow_grow)
    return false;

  VkDeviceSize cur = sgr->active->capacity;
  VkDeviceSize target = next_capacity(cur * 2, need_min);

  if (target < sgr->min_grow_capacity)
    target = sgr->min_grow_capacity;

  if (sgr->max_total_capacity != 0) {
    VkDeviceSize future_total = sgr->total_capacity + target;
    if (future_total > sgr->max_total_capacity)
      return false;
  }

  // Move active to retiring vector
  SgrBuffer *old = sgr->active;
  vec_push(&sgr->retiring, &old);

  // Create new active
  SgrBuffer *nb = NULL;
  if (!buffer_init(&nb, sgr->vma, target, sgr->frames_in_flight)) {
    // rollback: remove last push, restore old active
    vec_remove_at(&sgr->retiring, (u32)vec_len(&sgr->retiring) - 1);
    sgr->active = old;
    return false;
  }

  sgr->active = nb;
  sgr->total_capacity += target;
  return true;
}
