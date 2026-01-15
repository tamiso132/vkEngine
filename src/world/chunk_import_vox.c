//
#include "chunk_internal.h"
#include "vox_loader.h"
#include "world/chunk.h"

static void import_vox_models(ChunkTree *chunk, u32 palette_base, const VoxFile *vf, i32 base_x, i32 base_y, i32 base_z,
                              bool center_in_chunk) {
  u32 mc = (u32)vec_len((Vector *)&vf->models);

  for (u32 mi = 0; mi < mc; ++mi) {
    const VoxModel *m = VEC_AT((Vector *)&vf->models, mi, VoxModel);

    i32 sx = (m->sx > 0) ? m->sx : (i32)CHUNK_SIZE;
    i32 sy = (m->sy > 0) ? m->sy : (i32)CHUNK_SIZE;
    i32 sz = (m->sz > 0) ? m->sz : (i32)CHUNK_SIZE;

    i32 ox = base_x, oy = base_y, oz = base_z;
    if (center_in_chunk) {
      ox = ((i32)CHUNK_SIZE - sx) / 2;
      oy = ((i32)CHUNK_SIZE - sy) / 2;
      oz = ((i32)CHUNK_SIZE - sz) / 2;
    }

    u32 nv = (u32)vec_len((Vector *)&m->voxels);
    for (u32 vi = 0; vi < nv; ++vi) {
      const VoxVoxel *v = VEC_AT((Vector *)&m->voxels, vi, VoxVoxel);

      i32 x = ox + (i32)v->x;
      i32 y = oy + (i32)v->y;
      i32 z = oz + (i32)v->z;

      if ((u32)x >= (u32)CHUNK_SIZE)
        continue;
      if ((u32)y >= (u32)CHUNK_SIZE)
        continue;
      if ((u32)z >= (u32)CHUNK_SIZE)
        continue;

      u16 mat_index = (u16)(palette_base + vf->ci_to_used[v->ci]);
      _edit_set_voxel_color(chunk, x, y, z, true, mat_index);
    }
  }

  chunk->is_dirty = true;
}

bool chunk_import_vox_file(ChunkTree *chunk, const char *path, VoxAxisPreset vox_flags, bool center_in_chunk) {
  VoxFile vf = {};
  if (!vox_load(path, vox_flags, &vf, NULL))
    return false;

  // append used palette
  u32 palette_base = (u32)vec_len(&chunk->palette);
  for (u32 i = 0; i < (u32)vec_len(&vf.used_rgba); ++i) {
    u32 rgba = *VEC_AT(&vf.used_rgba, i, u32);
    vec_push(&chunk->palette, &rgba);
  }

  import_vox_models(chunk, palette_base, &vf, 0, 0, 0, center_in_chunk);
  // (free vf if needed by your loader)
  return true;
}
