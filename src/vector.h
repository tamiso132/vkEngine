#pragma once

#include "util.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// --- Allocator Interface ---
typedef struct Allocator {
  void *(*alloc)(size_t size, void *ctx);
  void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
  void (*free)(void *ptr, void *ctx);
  void *ctx; // User context (e.g., pointer to an Arena)
} Allocator;

// --- Vector Struct ---
typedef struct Vector {
  void *data;
  size_t length;
  size_t capacity;
  size_t element_size;
  Allocator *allocator;
} Vector;

// PUBLIC FUNCTIONS
 void *default_alloc(size_t size, void *ctx);
 void *default_realloc(void *ptr, size_t old, size_t new_s, void *ctx);
 void default_free(void *ptr, void *ctx);

// --- API ---
 void vec_init(Vector *vec, size_t elem_size, Allocator *allocator);
 void vec_init_with_capacity(Vector *vec, size_t capacity, size_t elem_size, Allocator *allocator);
 u32 vec_push(Vector *vec, void *element);
 void vec_remove_at(Vector *vec, u32 index);
 void vec_free(Vector *vec);
 void vec_clear(Vector *vec);
 void vec_realloc_capacity(Vector *vec, size_t new_cap);
 void *vec_at(Vector *vec, size_t index);
 u32 vec_len(Vector *vec);
 void vec_destroy(Vector *vec);

#define VEC_AT(vec, index, type) ((type *)vec_at((vec), index))

#define _CHECK_TYPE(T) _Static_assert(sizeof(T), "Type Validated: " #T)

#define _VT_1(T) _CHECK_TYPE(T)

#define _VT_2(a, b)                                                            \
  _CHECK_TYPE(a);                                                              \
  _CHECK_TYPE(b)

#define _VT_3(a, b, c)                                                         \
  _CHECK_TYPE(a);                                                              \
  _CHECK_TYPE(b);                                                              \
  _CHECK_TYPE(c)

#define _VT_4(a, b, c, d)                                                      \
  _CHECK_TYPE(a);                                                              \
  _CHECK_TYPE(b);                                                              \
  _CHECK_TYPE(c);                                                              \
  _CHECK_TYPE(d)

#define _VT_5(a, b, c, d, e)                                                   \
  _VT_4(a, b, c, d);                                                           \
  _CHECK_TYPE(e)

#define _GET_MACRO(_1, _2, _3, _4, _5, NAME, ...) NAME

#define VECTOR_TYPES(...)                                                      \
  _GET_MACRO(__VA_ARGS__, _VT_5, _VT_4, _VT_3, _VT_2, _VT_1)(__VA_ARGS__);
