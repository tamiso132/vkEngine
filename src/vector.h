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

// --- API ---
void vec_init(Vector *vec, size_t elem_size, Allocator *allocator);
void vec_push(Vector *vec, void *element);
void vec_realloc_capacity(Vector *vec, size_t new_cap);
void *vec_at(Vector *vec, size_t index);
void vec_destroy(Vector *vec);
