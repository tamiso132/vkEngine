
#pragma once

#include "world/chunk_internal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct ChunkTree ChunkTree; // opaque outside of chunk internals

// A CPU-only description of what a renderer/uploader may want to upload.
// Pointers remain valid until the next successful rebuild of the same chunk.
typedef struct ChunkUploadView {
  Vector p_res_data[CHUNK_RES__COUNT];
  uint64_t build_version;
} ChunkUploadView;

// PUBLIC FUNCTIONS

void chunk_build_if_needed(ChunkTree *chunk);
uint64_t chunk_build_version(const ChunkTree *chunk);

// ---- Lifecycle (CPU only) ----
ChunkTree *chunk_create(void);
void chunk_destroy(ChunkTree *chunk);
bool chunk_get_upload_view(const ChunkTree *chunk, ChunkUploadView *out_view);
bool chunk_is_dirty(const ChunkTree *chunk);
// END PUBLIC FUNCTIONS
