
#pragma once
#include "vector.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

// Iterates a contiguous array (no indirection).
typedef struct Iter {
  const unsigned char *base;
  size_t elem_size;
  size_t len;
  size_t at;
} Iter;

static inline Iter iter_make(const void *data, size_t len, size_t elem_size) {
  Iter it = {(const unsigned char *)data, elem_size, len, 0};
  return it;
}

static inline const void *iter_next(Iter *it) {
  if (!it || it->at >= it->len)
    return NULL;
  const void *p = it->base + it->at * it->elem_size;
  it->at++;
  return p;
}

#define ITER_NEXT(it, T) ((const T *)iter_next((it)))
#define ITER_NEXT_MUT(it, T) ((T *)iter_next((it)))

// ------------------------------------------------------------
// Indirect iterator: iterate an index list, yield elements from data array.
// Works for u32 indices (common in engines).
// ------------------------------------------------------------
typedef struct IndirectIter {
  const uint32_t *indices;
  size_t indices_len;
  size_t at;

  const unsigned char *data_base;
  size_t data_elem_size;
  size_t data_len; // optional safety clamp (can be 0 if you trust indices)
} IndirectIter;

static inline IndirectIter iiter_make_u32(const uint32_t *indices, size_t indices_len, const void *data,
                                          size_t data_len, size_t data_elem_size) {
  IndirectIter it;
  it.indices = indices;
  it.indices_len = indices_len;
  it.at = 0;
  it.data_base = (const unsigned char *)data;
  it.data_elem_size = data_elem_size;
  it.data_len = data_len;
  return it;
}

static inline IndirectIter iiter_make_from_vector(Vector indices, Vector data) {
  assert(indices.element_size == sizeof(u32));
  return iiter_make_u32(indices.data, indices.length, data.data, data.length, data.element_size);
}

// Returns pointer to next data element, or NULL when done.
// If data_len != 0, out-of-range indices are skipped defensively.
static inline const void *iiter_next(IndirectIter *it) {
  if (!it)
    return NULL;

  while (it->at < it->indices_len) {
    uint32_t idx = it->indices[it->at++];
    if (it->data_len != 0 && idx >= it->data_len) {
      continue; // skip bad index
    }
    return it->data_base + ((size_t)idx * it->data_elem_size);
  }
  return NULL;
}

#define IITER_FOREACH(item, T, it_ptr)                                                                                 \
  for (T *item = (T *)iiter_next((it_ptr)); item != NULL; item = (T *)iiter_next((it_ptr)))
