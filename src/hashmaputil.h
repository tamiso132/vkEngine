#pragma once
#include "common.h"
#include "hashmaputil.h"
/* ------------------------------------------------------------
   Typed map for STRING key field: item->KEYFIELD is const char*

   - Generates: NAME_hash, NAME_compare, NAME_new, NAME_set, NAME_get
   ------------------------------------------------------------ */
#define HM_TYPED_STRKEY(NAME, ITEM_T, KEYFIELD)                                                                        \
  static inline uint64_t NAME##_hash(const void *item, uint64_t s0, uint64_t s1) {                                     \
    const ITEM_T *it = (const ITEM_T *)item;                                                                           \
    const char *k = it->KEYFIELD ? it->KEYFIELD : "";                                                                  \
    return hashmap_sip(k, strlen(k), s0, s1);                                                                          \
  }                                                                                                                    \
  static inline int NAME##_compare(const void *a, const void *b, void *udata) {                                        \
    (void)udata;                                                                                                       \
    const ITEM_T *ia = (const ITEM_T *)a;                                                                              \
    const ITEM_T *ib = (const ITEM_T *)b;                                                                              \
    const char *ka = ia->KEYFIELD ? ia->KEYFIELD : "";                                                                 \
    const char *kb = ib->KEYFIELD ? ib->KEYFIELD : "";                                                                 \
    return strcmp(ka, kb);                                                                                             \
  }                                                                                                                    \
  static inline struct hashmap *NAME##_new(size_t initial_capacity) {                                                  \
    return hashmap_new(sizeof(ITEM_T), initial_capacity, 0, 0, NAME##_hash, NAME##_compare, NULL, NULL);               \
  }                                                                                                                    \
  static inline ITEM_T *NAME##_set(struct hashmap *map, ITEM_T item) { return (ITEM_T *)hashmap_set(map, &item); }     \
  static inline ITEM_T *NAME##_get(struct hashmap *map, const char *key) {                                             \
    ITEM_T tmp;                                                                                                        \
    memset(&tmp, 0, sizeof(tmp));                                                                                      \
    tmp.KEYFIELD = (char *)key;                                                                                        \
    return (ITEM_T *)hashmap_get(map, &tmp);                                                                           \
  }                                                                                                                    \
  static inline bool NAME##_del(struct hashmap *map, const char *key) {                                                \
    ITEM_T tmp;                                                                                                        \
    memset(&tmp, 0, sizeof(tmp));                                                                                      \
    tmp.KEYFIELD = (char *)key;                                                                                        \
    return hashmap_delete(map, &tmp) != NULL;                                                                          \
  }                                                                                                                    \
  /* Typed iteration convenience */                                                                                    \
  static inline bool NAME##_iter(struct hashmap *map, size_t *iter, ITEM_T **out) {                                    \
    void *item = NULL;                                                                                                 \
    if (!hashmap_iter(map, iter, &item))                                                                               \
      return false;                                                                                                    \
    *out = (ITEM_T *)item;                                                                                             \
    return true;                                                                                                       \
  }

/* ------------------------------------------------------------
   Typed map for U64 key field: item->KEYFIELD is uint64_t
   - Generates: NAME_hash, NAME_compare, NAME_new, NAME_set, NAME_get_u64
   ------------------------------------------------------------ */
#define HM_TYPED_U64KEY(NAME, ITEM_T, KEYFIELD)                                                                        \
  static inline uint64_t NAME##_hash(const void *item, uint64_t s0, uint64_t s1) {                                     \
    (void)s0;                                                                                                          \
    (void)s1;                                                                                                          \
    const ITEM_T *it = (const ITEM_T *)item;                                                                           \
    return hm_mix_u64((uint64_t)it->KEYFIELD);                                                                         \
  }                                                                                                                    \
  static inline int NAME##_compare(const void *a, const void *b, void *udata) {                                        \
    (void)udata;                                                                                                       \
    const ITEM_T *ia = (const ITEM_T *)a;                                                                              \
    const ITEM_T *ib = (const ITEM_T *)b;                                                                              \
    if (ia->KEYFIELD < ib->KEYFIELD)                                                                                   \
      return -1;                                                                                                       \
    if (ia->KEYFIELD > ib->KEYFIELD)                                                                                   \
      return 1;                                                                                                        \
    return 0;                                                                                                          \
  }                                                                                                                    \
  static inline struct hashmap *NAME##_new(size_t initial_capacity) {                                                  \
    return hashmap_new(sizeof(ITEM_T), initial_capacity, 0, 0, NAME##_hash, NAME##_compare, NULL, NULL);               \
  }                                                                                                                    \
  static inline ITEM_T *NAME##_set(struct hashmap *map, ITEM_T item) { return (ITEM_T *)hashmap_set(map, &item); }     \
  static inline ITEM_T *NAME##_get_u64(struct hashmap *map, uint64_t key) {                                            \
    ITEM_T tmp;                                                                                                        \
    memset(&tmp, 0, sizeof(tmp));                                                                                      \
    tmp.KEYFIELD = key;                                                                                                \
    return (ITEM_T *)hashmap_get(map, &tmp);                                                                           \
  }                                                                                                                    \
  static inline bool NAME##_del_u64(struct hashmap *map, uint64_t key) {                                               \
    ITEM_T tmp;                                                                                                        \
    memset(&tmp, 0, sizeof(tmp));                                                                                      \
    tmp.KEYFIELD = key;                                                                                                \
    return hashmap_delete(map, &tmp) != NULL;                                                                          \
  }                                                                                                                    \
  static inline bool NAME##_iter(struct hashmap *map, size_t *iter, ITEM_T **out) {                                    \
    void *item = NULL;                                                                                                 \
    if (!hashmap_iter(map, iter, &item))                                                                               \
      return false;                                                                                                    \
    *out = (ITEM_T *)item;                                                                                             \
    return true;                                                                                                       \
  }

// PUBLIC FUNCTIONS
uint64_t hm_mix_u64(uint64_t x);

inline uint64_t hm_pack_i21(int32_t v);
inline uint64_t hm_pack_vec3_i21(int32_t x, int32_t y, int32_t z);
// END PUBLIC FUNCTIONS
