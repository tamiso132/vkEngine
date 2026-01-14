
#pragma once

#include "common.h"
#include "util.h"
#include "vector.h"

typedef struct VoxVoxel {
  u8 x, y, z, ci;
} VoxVoxel;

typedef struct VoxModel {
  i32 sx, sy, sz; // SIZE chunk dims (after remap)
  Vector voxels;  // VoxVoxel[]
} VoxModel;

typedef struct VoxFile {
  u32 version;
  Vector models;         // VoxModel[]
  u32 palette_rgba[256]; // 0xRRGGBBAA
  u32 has_palette;

  Vector used_rgba;    // u32[]
  u16 ci_to_used[256]; // ci (0..255) -> local index into used_rgba, 0xFFFF if unused
} VoxFile;

// Pick how VOX axes map into YOUR engine axes.
// The loader will apply this remap while loading.
typedef enum VoxAxisPreset {
  VOX_AXIS_IDENTITY = 0,   // (x,y,z)
  VOX_AXIS_SWAP_YZ,        // (x,z,y)
  VOX_AXIS_SWAP_XY,        // (y,x,z)
  VOX_AXIS_SWAP_XZ,        // (z,y,x)
  VOX_AXIS_SWAP_YZ_FLIP_Y, // (x,z,-y)
  VOX_AXIS_SWAP_YZ_FLIP_Z, // (x,-z,y)
  VOX_AXIS_SWAP_YZ_FLIP_YZ // (x,-z,-y)
} VoxAxisPreset;

// PUBLIC FUNCTIONS
u32 vox_load(const char *path, VoxAxisPreset preset, VoxFile *out, Allocator *alloc);
void vox_free(VoxFile *vf);
void vox_build_used_palette(VoxFile *vf);

u32 vox_model_count(const VoxFile *vf);
VoxModel *vox_model_at(VoxFile *vf, u32 index);
