// chunk.c
#include "chunk.h"
#include "chunk_internal.h"
#include "common.h"
#include "raytrace.glsl"
#include "vox_loader.h"
#include <string.h>

// --- Private Prototypes ---

ChunkTree *chunk_init(M_Resource *rm) {
  ChunkTree *chunk = calloc(sizeof(ChunkTree), 1);
  vec_init(&chunk->nodes, sizeof(Node), NULL);
  vec_init(&chunk->child_indices, sizeof(ChildIndex), NULL);
  vec_init(&chunk->palette, sizeof(u32), NULL);

  chunk_import_vox_file(chunk, "assets/chr_knight.vox", VOX_AXIS_SWAP_YZ, false);
  chunk->is_dirty = true;
  chunk_rebuild_if_needed(chunk);
  _resource_init(chunk, rm);
  return chunk;
  // Example content lives elsewhere, not in init long-term.
}

void chunk_destroy(ChunkTree *chunk) {
  if (chunk->nodes.data)
    free(chunk->nodes.data);
  if (chunk->child_indices.data)
    free(chunk->child_indices.data);
  if (chunk->palette.data)
    free(chunk->palette.data);
  memset(chunk, 0, sizeof(*chunk));
}

void chunk_rebuild_if_needed(ChunkTree *chunk) {
  if (!chunk->is_dirty)
    return;

  ChunkBuildOutput out = {.child_indices = &chunk->child_indices, .nodes = &chunk->nodes};

  ChunkBuildInput input = {
      .chunk_size = CHUNK_SIZE, .bits = chunk->bits, .tree_levels = TREE_LEVELS, .vox_mat = chunk->vox_mat};
  _build_chunk(&input, NULL, &out);
  chunk->pending_edits = 0;
}

void chunk_tick(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd) {
  if (chunk->is_dirty) {

    ChunkBuildOutput out = {.child_indices = &chunk->child_indices, .nodes = &chunk->nodes};
    ChunkBuildInput input = {
        .chunk_size = CHUNK_SIZE, .bits = chunk->bits, .tree_levels = TREE_LEVELS, .vox_mat = chunk->vox_mat};

    _build_chunk(&input, NULL, &out);
    chunk->pending_edits = 0;
  }
  chunk->pending_edits = 0;
  _upload_chunk(chunk, gpu, rm, cmd);
}

ChunkGpuResources chunk_get_gpu_resource(ChunkTree *chunk) { return chunk->res; }
