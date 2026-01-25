#pragma once

#include <stdint.h>

typedef float f32;
typedef double f64;

typedef uint8_t u8;
typedef int8_t i8;

typedef uint16_t u16;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint32_t u32;
typedef uint64_t u64;

#define KIB(x) ((uint64_t)(x) * 1024ull)
#define MIB(x) (KIB(x) * 1024ull)
#define GIB(x) (MIB(x) * 1024ull)
#define TIB(x) (GIB(x) * 1024ull)

#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

typedef enum { RES_TYPE_BUFFER, RES_TYPE_IMAGE, RES_TYPE_COUNT, RES_TYPE_NONE } ResType;

typedef enum SystemType {
  SYSTEM_TYPE_GPU,
  SYSTEM_TYPE_RESOURCE,
  SYSTEM_TYPE_FILE,
  SYSTEM_TYPE_PIPELINE,
  SYSTEM_TYPE_HOTRELOAD,
  SYSTEM_TYPE_SUBMIT,
  SYSTEM_TYPE_COUNT,
} SystemType;

#define ASSERT_DEBUG true

#if ASSERT_DEBUG == true
// DEBUG MODE: Generate the "Trait" variable
#define SYSTEM_DECLARE_ID(type_struct, enum_id)                                                                        \
  typedef struct type_struct type_struct;                                                                              \
  static const SystemType _ID_##type_struct = enum_id

#else
// RELEASE MODE: Generate nothing
#define SYSTEM_DECLARE_ID(type_struct, enum_id)
#endif

typedef struct {
  u32 id : 31;
  ResType res_type : 2;
} ResHandle;

typedef uint32_t PipelineHandle;
typedef u32 Color;
// MANAGERS
SYSTEM_DECLARE_ID(M_GPU, SYSTEM_TYPE_GPU);
SYSTEM_DECLARE_ID(M_Resource, SYSTEM_TYPE_RESOURCE);
SYSTEM_DECLARE_ID(M_File, SYSTEM_TYPE_FILE);
SYSTEM_DECLARE_ID(M_Pipeline, SYSTEM_TYPE_PIPELINE);
SYSTEM_DECLARE_ID(M_HotReload, SYSTEM_TYPE_HOTRELOAD);
SYSTEM_DECLARE_ID(M_PipelineReloader, SYSTEM_TYPE_HOTRELOAD);
SYSTEM_DECLARE_ID(M_Submit, SYSTEM_TYPE_SUBMIT);
