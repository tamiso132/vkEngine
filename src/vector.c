#include "vector.h"
#include <assert.h>

// --- Private Prototypes ---
static void _resize(Vector *vec, u32 min_cap);
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

void *default_alloc(size_t size, void *ctx) { return malloc(size); }

void *default_realloc(void *ptr, size_t old, size_t new_s, void *ctx) { return realloc(ptr, new_s); }
void default_free(void *ptr, void *ctx) { free(ptr); }

Allocator std_allocator = {default_alloc, default_realloc, default_free, NULL};

void vec_init(Vector *vec, size_t elem_size, Allocator *allocator) {
  vec->data = NULL;
  vec->length = 0;
  vec->capacity = 0;
  vec->element_size = elem_size;
  vec->allocator = allocator ? allocator : &std_allocator;
}
void vec_init_with_capacity(Vector *vec, size_t capacity, size_t elem_size, Allocator *allocator) {
  vec_init(vec, elem_size, allocator);
  vec->capacity = capacity;
  vec->data = calloc(1, elem_size * capacity);
}
u32 vec_push(Vector *vec, void *element) {
  if (vec->length == vec->capacity) {
    _resize(vec, 0);
  }

  // memcpy is necessary for generic types in C
  void *dest = (char *)vec->data + (vec->length * vec->element_size);
  memcpy(dest, element, vec->element_size);
  vec->length++;

  return vec->length - 1;
}
u32 vec_append_zero(Vector *vec, u32 len) {
  u32 new_len = (vec->length + len);
  u32 old_len = vec->length;
  if (new_len >= vec->capacity) {
    _resize(vec, new_len * 2);
  }

  memset(&vec->data[old_len], 0, vec->element_size * len);
  return old_len;
}
void vec_remove_swap(Vector *vec, u32 index) {
  if (vec->length > 1 && index != (vec->length - 1)) {
    vec->length--;
    return;
  }

  void *src = &vec->data[(vec->length - 1) * vec->element_size];
  void *dst = &vec->data[index * vec->element_size];

  memcpy(dst, src, vec->element_size);
}
void vec_remove_at(Vector *vec, u32 index) {
  memmove(&vec->data[index], &vec[index + 1], vec->element_size * (vec->length - index));
  vec->length--;
}

void vec_copy(Vector *dst, Vector *src) {
  assert(dst->element_size == src->element_size);
  bool resize = dst->capacity <= src->length;
  if (resize) {
    _resize(dst, src->length);
  }

  memcpy(dst->data, src->data, src->length * src->element_size);
  dst->length = src->length;
}
void vec_free(Vector *vec) { vec->allocator->free(vec->data, vec->allocator->ctx); }

void vec_clear(Vector *vec) {
  assert(vec);
  memset(vec->data, 0, vec->length * vec->element_size);
  vec->length = 0;
}

void vec_realloc_capacity(Vector *vec, size_t new_cap) {
  vec->data = vec->allocator->realloc(vec->data, vec->element_size * vec->capacity, new_cap * vec->element_size,
                                      vec->allocator->ctx);
}

void vec_append(Vector *src, Vector *dst) {
  assert(dst->element_size == src->element_size);
  u32 new_len = src->length + dst->length;
  bool resize = dst->capacity <= new_len;
  if (resize) {
    _resize(dst, src->length);
  }

  memcpy(&dst->data[dst->length * dst->element_size], src->data, src->length * src->element_size);
  dst->length = new_len;
}

void *vec_at(const Vector *vec, size_t index) {
  if (index >= vec->length)
    return NULL;
  return (char *)vec->data + (index * vec->element_size);
}

u32 vec_len(const Vector *vec) { return vec->length; }
u32 vec_bytes_len(const Vector *vec) { return vec->length * vec->element_size; }

void vec_destroy(Vector *vec) {
  if (vec->data) {
    vec->allocator->free(vec->data, vec->allocator->ctx);
  }
  vec->data = NULL;
  vec->length = 0;
}

// --- Private Functions ---

static void _resize(Vector *vec, u32 min_cap) {
  size_t old_cap_bytes = vec->capacity * vec->element_size;
  vec->capacity = vec->capacity == 0 ? 8 : vec->capacity * 2;
  vec->capacity = MAX(vec->capacity, min_cap);

  size_t new_cap_bytes = vec->capacity * vec->element_size;

  vec->data = vec->allocator->realloc(vec->data, old_cap_bytes, new_cap_bytes, vec->allocator->ctx);
}
