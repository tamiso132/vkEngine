#pragma once
#include "common.h"
#include "hashmaputil.h"
#include "iterator.h"
#include "vector.h"
#include <cglm/types.h> // ivec3
#include <stdbool.h>
typedef struct ChunkTree ChunkTree;

/*
ChunkStore responsibility:
- Own chunk allocation / reuse (free list)
- Map coord -> (chunk_index + residence + list position)
- Maintain:
    - active_chunk_indices (for fast iteration/render)
    - cached_chunk_indices (for budget/eviction)
- Apply diffs:
    - entered coords -> ensure loaded + activate
    - left coords    -> move active -> cached
*/

typedef enum ChunkState {
  CHUNK_STATE_NONE,
  CHUNK_STATE_ACTIVE = 1,
  CHUNK_STATE_CACHED = 2,
} ChunkResidence;

typedef struct ChunkEntry {
  u32 chunk_index;
  ChunkResidence where;
  u32 active_pos; // valid if ACTIVE
  u32 cache_pos;  // valid if CACHED
} ChunkEntry;

typedef struct ChunkItem {
  ChunkTree *tree; // NULL means slot is free
  ivec3 coord;     // coord for this index (valid if tree != NULL)
} ChunkItem;

typedef struct ChunkStoreEntryItem {
  u64 key;        // hm_pack_vec3_i21(coord)
  ChunkEntry ent; // payload
} ChunkStoreEntryItem;

HM_TYPED_U64KEY(hm_chunk_store_entry, ChunkStoreEntryItem, key)

typedef struct ChunkStore {
  Vector chunk_items;           // ChunkItem[]
  Vector free_chunk_indices;    // u32[]
  Vector active_chunk_indices;  // u32[]
  Vector cached_chunk_indices;  // u32[]
  struct hashmap *coord_to_ent; // key->ChunkStoreEntryItem
  u32 max_cached;               // 0 means no cache limit
} ChunkStore;

typedef enum ChunkStoreResult {
  CHUNK_STORE_OK = 0,
  CHUNK_STORE_ERR_OOM,
  CHUNK_STORE_ERR_BAD_ARG,
} ChunkStoreResult;

// PUBLIC FUNCTIONS
void chunk_storage_get_active(ChunkStore *cs);
ChunkStoreResult chunk_store_apply_entered(ChunkStore *cs, const Vector *entered_coords);
ChunkStoreResult chunk_store_apply_left_to_cache(ChunkStore *cs, const Vector *left_coords);
ChunkTree *chunk_store_chunk_at(ChunkStore *cs, u32 chunk_index);
void chunk_store_destroy(ChunkStore *cs);
ChunkStoreResult chunk_store_init(ChunkStore *cs, u32 max_cached);

static inline IndirectIter chunk_store_get_active(ChunkStore *cs) {
  return iiter_make_from_vector(cs->active_chunk_indices, cs->chunk_items);
}
// END PUBLIC FUNCTIONS
