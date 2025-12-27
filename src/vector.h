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

// --- API ---

// --EASE
#define VEC_AT(vec, index, type) ((type *)vec_at(&vec, index))

// PUBLIC FUNCTIONS
void *default_alloc(size_t size, void *ctx);
void *default_realloc(void *ptr, size_t old, size_t new_s, void *ctx);
void default_free(void *ptr, void *ctx);

// --- API ---
void vec_init(Vector *vec, size_t elem_size, Allocator *allocator);
void vec_push(Vector *vec, void *element);
void vec_remove_at(Vector *vec, u32 index);
void vec_free(Vector *vec);
void vec_clear(Vector *vec);
void vec_realloc_capacity(Vector *vec, size_t new_cap);
void *vec_at(Vector *vec, size_t index);
u32 vec_len(Vector *vec);
void vec_destroy(Vector *vec);
