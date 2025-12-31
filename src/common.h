#pragma once

#include <stdint.h>

typedef uint8_t u8;
typedef int8_t i8;

typedef float f32;
typedef double f64;

typedef int32_t i32;
typedef int64_t i64;

typedef uint32_t u32;
typedef uint64_t u64;

typedef enum { RES_TYPE_BUFFER, RES_TYPE_IMAGE, RES_TYPE_COUNT } ResType;

typedef struct {
  u32 id : 31;
  ResType res_type : 1;
} ResHandle;

typedef uint32_t PipelineHandle;

// MANAGERS
typedef struct ResourceManager ResourceManager;
typedef struct M_Pipeline M_Pipeline;
