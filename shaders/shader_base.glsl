
#ifdef __STDC__
#pragma once
#endif

#ifdef __STDC__
#pragma once
// --- C Side ---
#include "common.h"
#include <cglm/cglm.h>
#include <stdalign.h>

// GCC/Clang syntax for alignment
#define ALIGN(number) __attribute__((aligned(number)))

// TRICK: Define the typedef alias FIRST, then start the struct definition.
// This allows: SHARED_STRUCT(Player, 16) { ... };
#define SHARED_STRUCT(name, align) \
        typedef struct name name; \
        struct ALIGN(align) name

#else
// --- GLSL Side ---
// Note: GLSL alignment is usually handled by std140/std430 layout rules,
// not manual attribute tags, so we define ALIGN as empty.
#define ALIGN(number)

#define SHARED_STRUCT(name, align) \
        struct name

// Type mappings
#define u8 uint
#define i8 int
#define u32 uint
#define i32 int
#define u64 uint64_t
#define i64 int64_t
#define f32 float
#define f64 double
#endif

// --- 2. Shared Constants ---
#define BINDING_SAMPLED 0
#define BINDING_STORAGE_IMAGE 1
#define BINDING_STORAGE_BUFFER 2
