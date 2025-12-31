#pragma once

// --- 1. Language Detection & Setup ---

#ifdef __STDC__
// --- C Side ---
#include "common.h"
#include <stdalign.h>

#define ALIGN(number) __attribute__((aligned(number)))

#else
// --- GLSL Side ---
#define u8 uint // Warning: See note below on packing!
#define i8 int
#define u32 uint
#define i32 int
#define u64 uint64_t // Requires GL_EXT_shader_explicit_arithmetic_types
#define i64 int64_t
#define f32 float
#define f64 double
#define ALIGN(number)
#endif

// --- 2. Shared Constants ---

#define BINDING_SAMPLED 0
#define BINDING_STORAGE_IMAGE 1
#define BINDING_STORAGE_BUFFER 2
