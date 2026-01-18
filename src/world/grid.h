// fixed_grid.h
#pragma once
#include "common.h"
#include "vector.h"
#include <cglm/types.h> // ivec3

typedef struct FixedGrid FixedGrid;

typedef enum FixedGridResult {
  FIXED_GRID_OK = 0,
  FIXED_GRID_ERR_BAD_CONFIG,
  FIXED_GRID_ERR_OOM,
} FixedGridResult;

// Callbacks receive lists of ivec3 coords (Vector element_size must be sizeof(ivec3)).
typedef void (*FixedGridOnEntered)(const Vector *entered_coords, void *user);
typedef void (*FixedGridOnLeft)(const Vector *left_coords, void *user);

// If you prefer a single callback:
typedef void (*FixedGridOnDiff)(const Vector *entered_coords, const Vector *left_coords, void *user);

typedef struct FixedGridCallbacks {
  FixedGridOnEntered on_entered; // optional
  FixedGridOnLeft on_left;       // optional
  FixedGridOnDiff on_diff;       // optional (called after entered/left, or instead — your choice)
  void *user;
} FixedGridCallbacks;

typedef struct FixedGridConfig {
  u32 visibility;       // V, window is VxVxV
  ivec3 initial_center; // starting center coord
  FixedGridCallbacks cb;
} FixedGridConfig;

// ---- Lifecycle ----
FixedGridResult fixed_grid_create(FixedGrid **out_grid, const FixedGridConfig *cfg);
void fixed_grid_destroy(FixedGrid *g);

// ---- State ----
void fixed_grid_get_center(const FixedGrid *g, ivec3 out_center);

// ---- Update ----
// Computes diff from old->new center, fills internal scratch vectors,
// then invokes callbacks with the coord lists.
// After this, center becomes new_center.
FixedGridResult fixed_grid_set_center(FixedGrid *g, ivec3 new_center);

// Convenience.
FixedGridResult fixed_grid_move_center(FixedGrid *g, ivec3 delta);

// ---- Optional: access last diff without callbacks ----
const Vector *fixed_grid_last_entered(const FixedGrid *g); // Vector<ivec3>
const Vector *fixed_grid_last_left(const FixedGrid *g);    // Vector<ivec3>
