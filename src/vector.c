#include "vector.h"

void *default_alloc(size_t size, void *ctx) { return malloc(size); }
void *default_realloc(void *ptr, size_t old, size_t new_s, void *ctx) {
  return realloc(ptr, new_s);
}
void default_free(void *ptr, void *ctx) { free(ptr); }

Allocator std_allocator = {default_alloc, default_realloc, default_free, NULL};

void vec_init(Vector *vec, size_t elem_size, Allocator *allocator) {
  vec->data = NULL;
  vec->length = 0;
  vec->capacity = 0;
  vec->element_size = elem_size;
  vec->allocator = allocator ? allocator : &std_allocator;
}

void vec_push(Vector *vec, void *element) {
  if (vec->length == vec->capacity) {
    size_t old_cap_bytes = vec->capacity * vec->element_size;
    vec->capacity = vec->capacity == 0 ? 8 : vec->capacity * 2;
    size_t new_cap_bytes = vec->capacity * vec->element_size;

    vec->data = vec->allocator->realloc(vec->data, old_cap_bytes, new_cap_bytes,
                                        vec->allocator->ctx);
  }

  // memcpy is necessary for generic types in C
  void *dest = (char *)vec->data + (vec->length * vec->element_size);
  memcpy(dest, element, vec->element_size);
  vec->length++;
}
void vec_realloc_capacity(Vector *vec, size_t new_cap) {
  vec->allocator->realloc(vec->data, vec->element_size * vec->capacity,
                          new_cap * vec->element_size, vec->allocator->ctx);
}
void *vec_at(Vector *vec, size_t index) {
  if (index >= vec->length)
    return NULL;
  return (char *)vec->data + (index * vec->element_size);
}

void vec_destroy(Vector *vec) {
  if (vec->data) {
    vec->allocator->free(vec->data, vec->allocator->ctx);
  }
  vec->data = NULL;
  vec->length = 0;
}
