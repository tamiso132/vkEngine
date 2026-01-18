#include "storage.h"
#include "cglm/ivec3.h"
#include "vector.h"
#include "world/chunk.h"
#include <string.h>

static inline u64 _key_from_coord(ivec3 c) { return hm_pack_vec3_i21(c[0], c[1], c[2]); }

static inline u32 _u32_at(const Vector *v, u32 i) { return ((u32 *)v->data)[i]; }
static inline void _u32_set(Vector *v, u32 i, u32 x) { ((u32 *)v->data)[i] = x; }

static inline ChunkItem *_item_at(ChunkStore *cs, u32 idx) { return &((ChunkItem *)cs->chunk_items.data)[idx]; }

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

static ChunkStoreEntryItem *_get_item(ChunkStore *cs, u64 key) {
  return hm_chunk_store_entry_get_u64(cs->coord_to_ent, key);
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
  ni.ent.where = CHUNK_RES_CACHED;
  ni.ent.cache_pos = (u32)vec_len(&cs->cached_chunk_indices);

  vec_push(&cs->cached_chunk_indices, &idx);
  hm_chunk_store_entry_set(cs->coord_to_ent, ni);

  return _get_item(cs, key);
}

static void _make_active(ChunkStore *cs, ChunkStoreEntryItem *it) {
  ChunkEntry *e = &it->ent;
  if (e->where == CHUNK_RES_ACTIVE)
    return;

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
  vec_push(&cs->active_chunk_indices, &e->chunk_index);
  e->where = CHUNK_RES_ACTIVE;
}

static void _make_cached(ChunkStore *cs, ChunkStoreEntryItem *it) {
  ChunkEntry *e = &it->ent;
  if (e->where == CHUNK_RES_CACHED)
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
  e->where = CHUNK_RES_CACHED;
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

ChunkStoreResult chunk_store_apply_left_to_cache(ChunkStore *cs, const Vector *left_coords) {
  if (!cs || !left_coords)
    return CHUNK_STORE_ERR_BAD_ARG;

  for (u32 i = 0; i < (u32)vec_len((Vector *)left_coords); ++i) {
    ivec3 c;
    glm_ivec3_copy(((ivec3 *)left_coords->data)[i], c);
    u64 key = _key_from_coord(c);

    ChunkStoreEntryItem *it = _get_item(cs, key);
    if (it)
      _make_cached(cs, it);
  }

  _evict_until_budget(cs);
  return CHUNK_STORE_OK;
}

ChunkStoreResult chunk_store_apply_entered(ChunkStore *cs, const Vector *entered_coords) {
  if (!cs || !entered_coords)
    return CHUNK_STORE_ERR_BAD_ARG;

  for (u32 i = 0; i < (u32)vec_len((Vector *)entered_coords); ++i) {
    ivec3 c;
    glm_ivec3_copy(((ivec3 *)entered_coords->data)[i], c);

    ChunkStoreEntryItem *it = _ensure_loaded(cs, c);
    if (it)
      _make_active(cs, it);
  }

  _evict_until_budget(cs);
  return CHUNK_STORE_OK;
}

ChunkTree *chunk_store_chunk_at(ChunkStore *cs, u32 chunk_index) {
  if (!cs)
    return NULL;
  if (chunk_index >= (u32)vec_len(&cs->chunk_items))
    return NULL;
  return _item_at(cs, chunk_index)->tree;
}
