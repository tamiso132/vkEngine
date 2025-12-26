#pragma once
#include <stdint.h>
#include <stdlib.h>

// --- Stretchy Buffer / Vector Implementation ---
typedef struct {
  size_t len;
  size_t cap;
} VecHdr;

#define v_hdr(v) ((VecHdr *)((char *)(v) - sizeof(VecHdr)))
#define v_len(v) ((v) ? v_hdr(v)->len : 0)
#define v_cap(v) ((v) ? v_hdr(v)->cap : 0)
#define v_push(v, val)                                                         \
  ((v) = _v_grow(v, sizeof(*(v))), (v)[v_hdr(v)->len++] = (val))
#define v_free(v) ((v) ? (free(v_hdr(v)), (v) = NULL) : 0)

static inline void *_v_grow(void *v, size_t elem_size) {
  size_t len = v_len(v);
  size_t cap = v_cap(v);
  if (len >= cap) {
    size_t new_cap = cap ? cap * 2 : 16;
    size_t new_size = sizeof(VecHdr) + new_cap * elem_size;
    VecHdr *new_hdr = (VecHdr *)realloc(v ? v_hdr(v) : NULL, new_size);
    new_hdr->len = len;
    new_hdr->cap = new_cap;
    return (void *)((char *)new_hdr + sizeof(VecHdr));
  }
  return v;
}

typedef float f32;
typedef double f64;

typedef int32_t i32;
typedef int64_t i64;

typedef uint32_t u32;
typedef uint64_t u64;
