// chunk.h
#pragma once
#include "command.h"
#include "common.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct ChunkTree ChunkTree; // opaque to users

typedef struct ChunkGpuResources {
  ResHandle gpu_node;
  ResHandle gpu_child_indices;
  ResHandle gpu_palette;
} ChunkGpuResources;

// PUBLIC FUNCTIONS

void chunk_tick(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd);

// lifecycle
ChunkTree *chunk_init(M_Resource *rm);
void chunk_destroy(ChunkTree *chunk);

void chunk_rebuild_if_needed(ChunkTree *chunk);

// editing (public)
bool chunk_get_voxel(const ChunkTree *chunk, int x, int y, int z);
void chunk_set_voxel(ChunkTree *chunk, int x, int y, int z, bool on);
void chunk_set_voxel_color(ChunkTree *chunk, int x, int y, int z, bool on, uint16_t mat);

ChunkGpuResources chunk_get_gpu_resource(ChunkTree *chunk);
