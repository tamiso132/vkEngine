//! chunk_api.h
#include "chunk_api.h"
#include "chunk_internal.h"
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "shaders/rt/rt_shared.glsl"
#include "vector.h"

// --- Private Prototypes ---
void chunk_init(ChunkInput input, Vector global_coords) {

  for (u32 i = 0; i < input.indices.length; i++) {
    u32 chunk_idx = *VEC_AT(&input.indices, i, u32);
    ChunkTree *chunk = VEC_AT(&input.trees, chunk_idx, ChunkTree);
    int *chunk_coord = (int *)vec_at(&global_coords, i);
    memcpy(chunk->min_corner, chunk_coord, sizeof(ivec3));
    vec_init(&chunk->view->p_res_data[CHUNK_RES_NODES], sizeof(Node), NULL);
    vec_init(&chunk->view->p_res_data[CHUNK_RES_CHILDREN], sizeof(ChildIndex), NULL);
    vec_init(&chunk->view->p_res_data[CHUNK_RES_PALETTE], sizeof(u32), NULL);
    vec_init(&chunk->view->p_res_data[CHUNK_RES_LEAF_MATS], sizeof(u16), NULL);

    // PUSH EMPTY ROOT
    vec_push(&chunk->view->p_res_data[CHUNK_RES_NODES], &(Node){.mask = 0});
    _edit_add_color(chunk, RGBA(255, 0, 0, 255));
    _edit_set_voxel(chunk, 5, 5, 5, true);
    _edit_set_voxel(chunk, 4, 5, 5, true);
    _edit_set_voxel(chunk, 3, 5, 5, true);
  }
  chunk_rebuild_cpu(input);
}

void chunk_destroy(ChunkInput *chunks) {
  if (!chunks)
    return;

  for (u32 i = 0; i < chunks->trees.length; i++) {
    ChunkTree *chunk = VEC_AT(&chunks->trees, i, ChunkTree);
    for (u32 i = 0; i < CHUNK_RES__COUNT; i++)
      vec_free(&chunk->view->p_res_data[i]);

    memset(chunk, 0, sizeof(*chunk));
    free(chunk);
  }
}

void chunk_rebuild_cpu(ChunkInput input) {

  ChunkBuildInput build_inputs[input.indices.length];
  ChunkBuildOutput build_outputs[input.indices.length];

  for (u32 i = 0; i < input.indices.length; i++) {
    u32 chunk_idx = *VEC_AT(&input.indices, i, u32);
    ChunkTree *chunk = VEC_AT(&input.trees, chunk_idx, ChunkTree);

    ChunkBuildInput input = {
        .chunk_size = CHUNK_SIZE, .tree_levels = TREE_LEVELS, .bits = chunk->bits, .vox_mat = chunk->vox_mat};
    ChunkBuildOutput output = {.child_indices = &chunk->view->p_res_data[CHUNK_RES_CHILDREN],
                               .leaf_mats = &chunk->view->p_res_data[CHUNK_RES_LEAF_MATS],
                               .nodes = &chunk->view->p_res_data[CHUNK_RES_NODES]};
    memcpy(&build_inputs[i], &input, sizeof(ChunkBuildInput));
    memcpy(&build_outputs[i], &output, sizeof(ChunkBuildOutput));
  }

  ChunkBuildResult r = _build_chunks(build_inputs, NULL, build_outputs, input.indices.length);

  // If build fails, keep dirty so you can retry later.
}

bool chunk_get_upload_view(const ChunkInput input, ChunkUploadView *out_view) {
  assert(input.trees.data);
  assert(input.trees.length > 0);

  for (u32 i = 0; i < input.indices.length; i++) {
    u32 chunk_idx = *VEC_AT(&input.indices, i, u32);
    ChunkTree *chunk = VEC_AT(&input.trees, chunk_idx, ChunkTree);
    memcpy(&out_view[i], chunk->view, sizeof(ChunkUploadView));
  }
  return true;
}
// --- Private Functions ---
