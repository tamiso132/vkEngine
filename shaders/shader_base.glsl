#ifndef SHARED_BASE_H
#define SHARED_BASE_H

#if !defined(VKENGINE_C)
// -------- GLSL side --------
#define ALIGN(number)
#define SHARED_STRUCT(name, align) struct name

#define u8  uint
#define i8  int
#define u32 uint
#define i32 int
#define u64 uint64_t
#define i64 int64_t
#define f32 float
#define f64 double

#define SHARED_CONST_U32(name, value) const uint name = value

#else
// -------- C side --------
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define ALIGN(number) __attribute__((aligned(number)))
#else
#define ALIGN(number)
#endif

#define SHARED_STRUCT(name, align) \
    typedef struct name name;      \
    struct ALIGN(align) name

// compile-time constants (switch/case, array sizes)
#define SHARED_CONST_U32(name, value) enum { name = (value) }

#endif // __VERSION__

// Shared bindings
#define BINDING_SAMPLED_IMAGE  0
#define BINDING_STORAGE_IMAGE  1
#define BINDING_STORAGE_BUFFER 2

#endif // SHARED_BASE_H
