
#include "chunk.h"
#include "chunk_internal.h"
#include <stdlib.h>
#include <string.h>

#include "shaders/rt/rt_shared.glsl"

// --- Private Prototypes ---
ChunkTree *chunk_create(void) {
  ChunkTree *chunk = (ChunkTree *)calloc(1, sizeof(ChunkTree));
  if (!chunk)
    return NULL;

  vec_init(&chunk->p_res_data[CHUNK_RES_NODES], sizeof(Node), NULL);
  vec_init(&chunk->p_res_data[CHUNK_RES_CHILDREN], sizeof(ChildIndex), NULL);
  vec_init(&chunk->p_res_data[CHUNK_RES_CHILDREN], sizeof(u32), NULL);
  vec_init(&chunk->p_res_data[CHUNK_RES_LEAF_MATS], sizeof(u16), NULL);

  // PUSH EMPTY ROOT
  vec_push(&chunk->p_res_data[CHUNK_RES_NODES], &(Node){.mask = 0});
  chunk->is_dirty = true;

  chunk->build_version = 0;

  chunk_build_if_needed(chunk);
  return chunk;
}

void chunk_destroy(ChunkTree *chunk) {
  if (!chunk)
    return;

  for (u32 i = 0; i < CHUNK_RES__COUNT; i++)
    vec_free(&chunk->p_res_data[i]);

  memset(chunk, 0, sizeof(*chunk));
  free(chunk);
}

uint64_t chunk_build_version(const ChunkTree *chunk) { return chunk ? chunk->build_version : 0; }
bool chunk_is_dirty(const ChunkTree *chunk) { return chunk ? chunk->is_dirty : false; }

void chunk_build_if_needed(ChunkTree *chunk) {
  if (!chunk || !chunk->is_dirty)
    return;

  ChunkBuildOutput out = {
      .child_indices = &chunk->p_res_data[CHUNK_RES_CHILDREN],
      .nodes = &chunk->p_res_data[CHUNK_RES_NODES],
      .leaf_mats = &chunk->p_res_data[CHUNK_RES_LEAF_MATS],
  };

  ChunkBuildInput input = {
      .chunk_size = CHUNK_SIZE,
      .bits = chunk->bits,
      .tree_levels = TREE_LEVELS,
      .vox_mat = chunk->vox_mat,
  };

  ChunkBuildResult r = _build_chunk(&input, NULL, &out);
  if (r == CHUNK_BUILD_OK) {
    chunk->is_dirty = false;
    chunk->build_version += 1;
  }
  // If build fails, keep dirty so you can retry later.
}

bool chunk_get_upload_view(const ChunkTree *chunk, ChunkUploadView *out_view) {
  if (!chunk || !out_view)
    return false;

  if (chunk->build_version == 0)
    return false;

  memcpy(out_view->p_res_data, chunk->p_res_data, sizeof(chunk->p_res_data));
  return true;
}
// --- Private Functions ---
