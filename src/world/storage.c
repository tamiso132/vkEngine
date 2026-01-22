#include "storage.h"
#include "cglm/ivec3.h"
#include "res_async.h"
#include "resource/rm_internal.h"
#include "vector.h"
#include "world/chunk.h"
#include "world/chunk_gpu.h"
#include <string.h>

// --- Private Prototypes ---
static u32 _alloc_index(ChunkStore *cs);

static ChunkStoreEntryItem *_ensure_loaded(ChunkStore *cs, ivec3 coord);

static void _evict_until_budget(ChunkStore *cs);

static ChunkStoreEntryItem *_get_item(ChunkStore *cs, u64 key);

static inline ChunkItem *_item_at(ChunkStore *cs, u32 idx);

static inline u64 _key_from_coord(ivec3 c);

static i32 _make_active(ChunkStore *cs, ChunkStoreEntryItem *it);
static void _make_cached(ChunkStore *cs, ChunkStoreEntryItem *it);

static inline u32 _u32_at(const Vector *v, u32 i);
static inline void _u32_set(Vector *v, u32 i, u32 x);

ChunkStoreResult chunk_store_init(ChunkStore *cs, u32 max_cached) {
  if (!cs)
    return CHUNK_STORE_ERR_BAD_ARG;
  memset(cs, 0, sizeof(*cs));

  cs->max_cached = max_cached;

  vec_init(&cs->chunk_items, sizeof(ChunkItem), NULL);
  vec_init(&cs->free_chunk_indices, sizeof(u32), NULL);
  vec_init(&cs->active_chunk_indices, sizeof(u32), NULL);
  vec_init(&cs->cached_chunk_indices, sizeof(u32), NULL);

  cs->coord_to_ent = hm_chunk_store_entry_new(256);
  if (!cs->coord_to_ent)
    return CHUNK_STORE_ERR_OOM;

  return CHUNK_STORE_OK;
}

ChunkDescriptorIndices chunk_store_get_descriptors(ChunkStore *cs, M_Resource *rm, u32 active_idx) {
  u32 chunk_idx = *VEC_AT(&cs->active_chunk_indices, active_idx, u32);
  ChunkItem *item = VEC_AT(&cs->chunk_items, chunk_idx, ChunkItem);
  return chunk_gpu_get_descriptor_indices(item->chunk_gpu, rm);
}
void chunk_store_destroy(ChunkStore *cs) {
  if (!cs)
    return;

  // destroy any live chunks
  for (u32 i = 0; i < (u32)vec_len(&cs->chunk_items); ++i) {
    ChunkItem *it = _item_at(cs, i);
    if (it->tree) {
      chunk_destroy(it->tree);
      it->tree = NULL;
    }
  }

  if (cs->coord_to_ent) {
    hashmap_free(cs->coord_to_ent);
    cs->coord_to_ent = NULL;
  }

  vec_free(&cs->chunk_items);
  vec_free(&cs->free_chunk_indices);
  vec_free(&cs->active_chunk_indices);
  vec_free(&cs->cached_chunk_indices);
}

ChunkStoreResult chunk_store_apply_left_to_cache(ChunkStore *cs, const Vector left_coords) {
  if (!cs || !left_coords.data)
    return CHUNK_STORE_ERR_BAD_ARG;

  for (u32 i = 0; i < (u32)vec_len(&left_coords); ++i) {
    ivec3 c;
    glm_ivec3_copy(((ivec3 *)left_coords.data)[i], c);
    u64 key = _key_from_coord(c);

    ChunkStoreEntryItem *it = _get_item(cs, key);
    if (it)
      _make_cached(cs, it);
  }

  _evict_until_budget(cs);
  return CHUNK_STORE_OK;
}

ChunkStoreResult chunk_store_apply_left_idxs(ChunkStore *cs, const Vector left_idxs) {

  for (u32 i = 0; i < vec_len(&left_idxs); ++i) {
    ChunkItem *item = VEC_AT(&cs->chunk_items, *VEC_AT(&cs->active_chunk_indices, i, u32), ChunkItem);
    ivec3 c = {};
    glm_ivec3_copy(((ivec3 *)item->coord)[i], c);
    u64 key = _key_from_coord(c);
    ChunkStoreEntryItem *it = _get_item(cs, key);
    if (it)
      _make_cached(cs, it);
  }
  _evict_until_budget(cs);
  return CHUNK_STORE_OK;
}

void chunk_store_gpu_tick(ChunkStore *cs, M_Resource *rm, TransferQueue *transfer) {
  for (u32 i = 0; i < cs->active_chunk_indices.length; i++) {
    u32 active_idx = *VEC_AT(&cs->active_chunk_indices, i, u32);
    ChunkItem item = *VEC_AT(&cs->chunk_items, active_idx, ChunkItem);
    assert(item.chunk_gpu);

    chunk_gpu_tick(item.chunk_gpu, rm, transfer);
  }
}
ChunkApplyResult chunk_store_apply_entered(ChunkStore *cs, const Vector *entered_coords) {

  ChunkApplyResult result = {.err_code = CHUNK_STORE_ERR_BAD_ARG};
  if (!cs || !entered_coords)
    return result;

  vec_init(&result.chunk_idxs, sizeof(u32), NULL);

  for (u32 i = 0; i < (u32)vec_len((Vector *)entered_coords); ++i) {
    ivec3 c;
    glm_ivec3_copy(((ivec3 *)entered_coords->data)[i], c);

    ChunkStoreEntryItem *it = _ensure_loaded(cs, c);
    u32 active_idx = it->ent.active_pos;
    if (it)
      active_idx = _make_active(cs, it);

    vec_push(&result.chunk_idxs, &active_idx);
  }

  _evict_until_budget(cs);
  return result;
}

ChunkTree *chunk_store_chunk_at(ChunkStore *cs, u32 chunk_index) {
  if (!cs)
    return NULL;
  if (chunk_index >= (u32)vec_len(&cs->chunk_items))
    return NULL;
  return _item_at(cs, chunk_index)->tree;
}

void chunk_storage_get_active(ChunkStore *cs) {}

// --- Private Functions ---

static u32 _alloc_index(ChunkStore *cs) {
  if (vec_len(&cs->free_chunk_indices) > 0) {
    u32 last = (u32)vec_len(&cs->free_chunk_indices) - 1u;
    u32 idx = _u32_at(&cs->free_chunk_indices, last);
    cs->free_chunk_indices.length = last;
    return idx;
  }
  // grow chunk_items
  ChunkItem it;
  memset(&it, 0, sizeof(it));
  vec_push(&cs->chunk_items, &it);
  return (u32)vec_len(&cs->chunk_items) - 1u;
}

static ChunkStoreEntryItem *_ensure_loaded(ChunkStore *cs, ivec3 coord) {
  u64 key = _key_from_coord(coord);
  ChunkStoreEntryItem *found = _get_item(cs, key);
  if (found)
    return found;

  u32 idx = _alloc_index(cs);
  ChunkItem *slot = _item_at(cs, idx);

  // allocate chunk if slot was free
  if (slot->tree == NULL) {
    slot->tree = chunk_create();
  }
  glm_ivec3_copy(coord, slot->coord);

  // insert as CACHED (then caller can activate)
  ChunkStoreEntryItem ni;
  memset(&ni, 0, sizeof(ni));
  ni.key = key;
  ni.ent.chunk_index = idx;
  ni.ent.where = CHUNK_STATE_CACHED;
  ni.ent.cache_pos = (u32)vec_len(&cs->cached_chunk_indices);

  vec_push(&cs->cached_chunk_indices, &idx);
  hm_chunk_store_entry_set(cs->coord_to_ent, ni);

  return _get_item(cs, key);
}

static void _evict_until_budget(ChunkStore *cs) {
  if (cs->max_cached == 0)
    return;

  while (vec_len(&cs->cached_chunk_indices) > cs->max_cached) {
    u32 last = (u32)vec_len(&cs->cached_chunk_indices) - 1u;
    u32 idx = _u32_at(&cs->cached_chunk_indices, last);
    cs->cached_chunk_indices.length = last;

    ChunkItem *slot = _item_at(cs, idx);
    u64 key = _key_from_coord(slot->coord);

    // delete map entry
    hm_chunk_store_entry_del_u64(cs->coord_to_ent, key);

    // destroy chunk + free slot
    if (slot->tree) {
      chunk_destroy(slot->tree);
      slot->tree = NULL;
    }
    vec_push(&cs->free_chunk_indices, &idx);
  }
}

static ChunkStoreEntryItem *_get_item(ChunkStore *cs, u64 key) {
  return hm_chunk_store_entry_get_u64(cs->coord_to_ent, key);
}

static inline ChunkItem *_item_at(ChunkStore *cs, u32 idx) { return &((ChunkItem *)cs->chunk_items.data)[idx]; }

static inline u64 _key_from_coord(ivec3 c) { return hm_pack_vec3_i21(c[0], c[1], c[2]); }

static i32 _make_active(ChunkStore *cs, ChunkStoreEntryItem *it) {
  ChunkEntry *e = &it->ent;
  if (e->where == CHUNK_STATE_ACTIVE)
    return e->active_pos;

  // remove from cached list via swap-remove
  u32 removed_pos = e->cache_pos;
  u32 last_pos = (u32)vec_len(&cs->cached_chunk_indices) - 1u;

  if (removed_pos != last_pos) {
    u32 moved_idx = *VEC_AT(&cs->cached_chunk_indices, removed_pos, u32);
    vec_remove_swap(&cs->cached_chunk_indices, removed_pos);

    // update moved entry cache_pos
    ChunkItem *moved_item = _item_at(cs, moved_idx);
    u64 moved_key = _key_from_coord(moved_item->coord);
    ChunkStoreEntryItem *moved_ent = _get_item(cs, moved_key);
    if (moved_ent)
      moved_ent->ent.cache_pos = removed_pos;
  } else {
    cs->cached_chunk_indices.length = last_pos;
  }

  // append to active
  e->active_pos = (u32)vec_len(&cs->active_chunk_indices);
  e->where = CHUNK_STATE_ACTIVE;
  return vec_push(&cs->active_chunk_indices, &e->chunk_index);
}

static void _make_cached(ChunkStore *cs, ChunkStoreEntryItem *it) {
  ChunkEntry *e = &it->ent;
  if (e->where == CHUNK_STATE_CACHED)
    return;

  // remove from active list via swap-remove
  u32 removed_pos = e->active_pos;
  u32 last_pos = (u32)vec_len(&cs->active_chunk_indices) - 1u;

  if (removed_pos != last_pos) {
    u32 moved_idx = *VEC_AT(&cs->cached_chunk_indices, removed_pos, u32);
    vec_remove_swap(&cs->active_chunk_indices, removed_pos);

    // update moved entry active_pos
    ChunkItem *moved_item = _item_at(cs, moved_idx);
    u64 moved_key = _key_from_coord(moved_item->coord);
    ChunkStoreEntryItem *moved_ent = _get_item(cs, moved_key);
    if (moved_ent)
      moved_ent->ent.active_pos = removed_pos;
  } else {
    cs->active_chunk_indices.length = last_pos;
  }

  // append to cache
  e->cache_pos = (u32)vec_len(&cs->cached_chunk_indices);
  vec_push(&cs->cached_chunk_indices, &e->chunk_index);
  e->where = CHUNK_STATE_CACHED;
}

static inline u32 _u32_at(const Vector *v, u32 i) { return ((u32 *)v->data)[i]; }

static inline void _u32_set(Vector *v, u32 i, u32 x) { ((u32 *)v->data)[i] = x; }
