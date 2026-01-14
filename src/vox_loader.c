
#include "vox_loader.h"

#include <stdio.h>
#include <string.h>

// ----------------- axis remap (STATIC, INTERNAL) -----------------
typedef struct AxisMap {
  u8 dst_from_src[3]; // dst axis gets value from src axis (0=x,1=y,2=z)
  u8 flip[3];         // flip within that src dimension
} AxisMap;

// --- Private Prototypes ---
static void _used_palette_init(VoxFile *vf, Allocator *alloc);
static void _used_palette_free(VoxFile *vf);
static u32 rd_u32(FILE *f);
static u32 rd_tag(FILE *f, char out[5]);
static void default_palette(u32 pal[256]);
static void vox_model_init(VoxModel *m, Allocator *alloc);
static void vox_model_destroy(VoxModel *m);
static AxisMap axis_map_from_preset(VoxAxisPreset p);
static void apply_map_dims(i32 sx, i32 sy, i32 sz, AxisMap m, i32 *dx, i32 *dy, i32 *dz);
static void apply_map_coord(i32 sx, i32 sy, i32 sz, i32 x, i32 y, i32 z, AxisMap m, i32 *ox, i32 *oy, i32 *oz);

u32 vox_load(const char *path, VoxAxisPreset preset, VoxFile *out, Allocator *alloc) {
  memset(out, 0, sizeof(*out));
  vec_init(&out->models, sizeof(VoxModel), alloc);
  vec_init(&out->used_rgba, sizeof(Color), alloc);
  default_palette(out->palette_rgba);

  AxisMap map = axis_map_from_preset(preset);

  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;

  char tag[5];

  // "VOX " header
  if (!rd_tag(f, tag) || strcmp(tag, "VOX ") != 0) {
    fclose(f);
    return 0;
  }
  out->version = rd_u32(f);

  // MAIN
  if (!rd_tag(f, tag) || strcmp(tag, "MAIN") != 0) {
    fclose(f);
    return 0;
  }
  (void)rd_u32(f); // main content size
  u32 main_children = rd_u32(f);

  long main_end = ftell(f) + (long)main_children;

  // pending model until we see XYZI
  VoxModel pending;
  u32 have_pending = 0;
  i32 src_sx = 0, src_sy = 0, src_sz = 0; // original SIZE dims (VOX space)

  while (ftell(f) < main_end) {
    char cid[5];
    if (!rd_tag(f, cid))
      break;

    u32 content = rd_u32(f);
    u32 children = rd_u32(f);
    long content_start = ftell(f);

    if (strcmp(cid, "SIZE") == 0 && content >= 12u) {
      src_sx = (i32)rd_u32(f);
      src_sy = (i32)rd_u32(f);
      src_sz = (i32)rd_u32(f);

      if (!have_pending) {
        vox_model_init(&pending, alloc);
        have_pending = 1;
      }

      // store remapped dims in pending
      apply_map_dims(src_sx, src_sy, src_sz, map, &pending.sx, &pending.sy, &pending.sz);
    } else if (strcmp(cid, "XYZI") == 0 && content >= 4u) {
      if (!have_pending) {
        // if SIZE missing (rare), assume some dims; flips won't be meaningful, but still works for swaps.
        src_sx = src_sy = src_sz = 256;
        vox_model_init(&pending, alloc);
        have_pending = 1;
        apply_map_dims(src_sx, src_sy, src_sz, map, &pending.sx, &pending.sy, &pending.sz);
      }

      u32 nvox = rd_u32(f);

      for (u32 i = 0; i < nvox; ++i) {
        VoxVoxel v;
        if (fread(&v, 1, sizeof(VoxVoxel), f) != sizeof(VoxVoxel)) {
          vox_model_destroy(&pending);
          fclose(f);
          return 0;
        }

        // remap coords right here
        i32 rx, ry, rz;
        apply_map_coord(src_sx, src_sy, src_sz, (i32)v.x, (i32)v.y, (i32)v.z, map, &rx, &ry, &rz);

        VoxVoxel outv;
        outv.x = (u8)rx;
        outv.y = (u8)ry;
        outv.z = (u8)rz;
        outv.ci = v.ci;

        vec_push(&pending.voxels, &outv);
      }

      // finalize model
      vec_push(&out->models, &pending);
      have_pending = 0;
      memset(&pending, 0, sizeof(pending));
      src_sx = src_sy = src_sz = 0;
    } else if (strcmp(cid, "RGBA") == 0 && content >= 1024u) {
      u8 rgba[1024];
      if (fread(rgba, 1, 1024, f) == 1024) {
        for (u32 i = 0; i < 256u; ++i) {
          u8 r = rgba[i * 4u + 0u];
          u8 g = rgba[i * 4u + 1u];
          u8 b = rgba[i * 4u + 2u];
          u8 a = rgba[i * 4u + 3u];
          out->palette_rgba[i] = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | ((u32)a);
        }
        out->has_palette = 1u;
      }
    }

    // skip remainder of content
    fseek(f, content_start + (long)content, SEEK_SET);
    // skip children chunks (scene graph etc.)
    if (children)
      fseek(f, (long)children, SEEK_CUR);
  }

  fclose(f);

  if (have_pending)
    vox_model_destroy(&pending);

  vox_build_used_palette(out);
  return vec_len(&out->models) > 0 ? 1u : 0u;
}

void vox_build_used_palette(VoxFile *vf) {
  // reset
  vec_clear(&vf->used_rgba);
  for (int i = 0; i < 256; ++i)
    vf->ci_to_used[i] = 0xFFFFu;

  // 0 is "empty" in our usage; MagicaVoxel ci is typically 1..255
  // We'll only collect ci that appear in voxels.
  u32 model_count = (u32)vec_len(&vf->models);
  for (u32 mi = 0; mi < model_count; ++mi) {
    VoxModel *m = VEC_AT(&vf->models, mi, VoxModel);
    u32 nvox = (u32)vec_len(&m->voxels);

    for (u32 vi = 0; vi < nvox; ++vi) {
      VoxVoxel *v = VEC_AT(&m->voxels, vi, VoxVoxel);
      u8 ci = v->ci;
      if (ci == 0)
        continue; // ignore

      if (vf->ci_to_used[ci] != 0xFFFFu)
        continue; // already added

      // ci=1 maps to palette[0]
      u32 rgba = vf->palette_rgba[(u32)ci - 1u];

      u16 local = (u16)vec_len(&vf->used_rgba);
      vec_push(&vf->used_rgba, &rgba);
      vf->ci_to_used[ci] = local;
    }
  }
}
void vox_free(VoxFile *vf) {
  if (!vf)
    return;

  u32 n = (u32)vec_len(&vf->models);
  for (u32 i = 0; i < n; ++i) {
    VoxModel *m = VEC_AT(&vf->models, i, VoxModel);
    vox_model_destroy(m);
  }

  vec_free(&vf->models);
  memset(vf, 0, sizeof(*vf));
}

// --- Private Functions ---

static void _used_palette_init(VoxFile *vf, Allocator *alloc) {
  vec_init(&vf->used_rgba, sizeof(u32), alloc);
  for (int i = 0; i < 256; ++i)
    vf->ci_to_used[i] = 0xFFFFu;
}

static void _used_palette_free(VoxFile *vf) {
  vec_free(&vf->used_rgba);
  for (int i = 0; i < 256; ++i)
    vf->ci_to_used[i] = 0xFFFFu;
}

// ----------------- little-endian readers -----------------
static u32 rd_u32(FILE *f) {
  u8 b[4];
  if (fread(b, 1, 4, f) != 4)
    return 0;
  return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

static u32 rd_tag(FILE *f, char out[5]) {
  if (fread(out, 1, 4, f) != 4)
    return 0;
  out[4] = 0;
  return 1;
}

static void default_palette(u32 pal[256]) {
  for (u32 i = 0; i < 256u; ++i) {
    u8 c = (u8)i;
    pal[i] = ((u32)c << 24) | ((u32)c << 16) | ((u32)c << 8) | 0xFFu; // RRGGBBAA
  }
}

// ----------------- model helpers -----------------
static void vox_model_init(VoxModel *m, Allocator *alloc) {
  memset(m, 0, sizeof(*m));
  vec_init(&m->voxels, sizeof(VoxVoxel), alloc);
}

static void vox_model_destroy(VoxModel *m) {
  vec_free(&m->voxels);
  memset(m, 0, sizeof(*m));
}

static AxisMap axis_map_from_preset(VoxAxisPreset p) {
  AxisMap m = {{0, 1, 2}, {0, 0, 0}}; // identity

  switch (p) {
  default:
  case VOX_AXIS_IDENTITY:
    break;

  case VOX_AXIS_SWAP_YZ: // x,z,y
    m.dst_from_src[0] = 0;
    m.dst_from_src[1] = 2;
    m.dst_from_src[2] = 1;
    break;

  case VOX_AXIS_SWAP_XY: // y,x,z
    m.dst_from_src[0] = 1;
    m.dst_from_src[1] = 0;
    m.dst_from_src[2] = 2;
    break;

  case VOX_AXIS_SWAP_XZ: // z,y,x
    m.dst_from_src[0] = 2;
    m.dst_from_src[1] = 1;
    m.dst_from_src[2] = 0;
    break;

  case VOX_AXIS_SWAP_YZ_FLIP_Y: // x,z,-y  (flip dst.z which comes from src.y)
    m.dst_from_src[0] = 0;
    m.dst_from_src[1] = 2;
    m.dst_from_src[2] = 1;
    m.flip[2] = 1;
    break;

  case VOX_AXIS_SWAP_YZ_FLIP_Z: // x,-z,y  (flip dst.y which comes from src.z)
    m.dst_from_src[0] = 0;
    m.dst_from_src[1] = 2;
    m.dst_from_src[2] = 1;
    m.flip[1] = 1;
    break;

  case VOX_AXIS_SWAP_YZ_FLIP_YZ: // x,-z,-y
    m.dst_from_src[0] = 0;
    m.dst_from_src[1] = 2;
    m.dst_from_src[2] = 1;
    m.flip[1] = 1;
    m.flip[2] = 1;
    break;
  }
  return m;
}

static void apply_map_dims(i32 sx, i32 sy, i32 sz, AxisMap m, i32 *dx, i32 *dy, i32 *dz) {
  i32 dim[3] = {sx, sy, sz};
  *dx = dim[m.dst_from_src[0]];
  *dy = dim[m.dst_from_src[1]];
  *dz = dim[m.dst_from_src[2]];
}

static void apply_map_coord(i32 sx, i32 sy, i32 sz, i32 x, i32 y, i32 z, AxisMap m, i32 *ox, i32 *oy, i32 *oz) {
  i32 src[3] = {x, y, z};
  i32 dim[3] = {sx, sy, sz};

  i32 dst[3];
  for (int a = 0; a < 3; ++a) {
    int s = (int)m.dst_from_src[a];
    i32 v = src[s];
    if (m.flip[a])
      v = (dim[s] - 1) - v;
    dst[a] = v;
  }

  *ox = dst[0];
  *oy = dst[1];
  *oz = dst[2];
}
