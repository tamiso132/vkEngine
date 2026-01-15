//! chunk_internal.h
#include "chunk.h"
#include "chunk_internal.h"
#include "command.h"

// --- Private Prototypes ---

void _resource_init(ChunkTree *chunk, M_Resource *rm) {
  RGBufferInfo node_info = {.name = "NodeBuffer",
                            .capacity = vec_bytes_len(&chunk->nodes),
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo child_info = {.name = "childIndexBuffer",
                             .capacity = vec_bytes_len(&chunk->child_indices),
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  RGBufferInfo pal_info = {.name = "Palette",
                           .capacity = vec_bytes_len(&chunk->palette),
                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT};

  chunk->res.gpu_child_indices = rm_create_buffer(rm, &child_info);
  chunk->res.gpu_node = rm_create_buffer(rm, &node_info);
  chunk->res.gpu_palette = rm_create_buffer(rm, &pal_info);

  // After creating GPU buffers, you likely want an upload
  chunk->need_upload = true;
}

void _upload_chunk(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd) {
  if (!chunk->need_upload)
    return;

  cmd_buffer_upload(cmd, gpu, rm, chunk->res.gpu_node, chunk->nodes.data, vec_bytes_len(&chunk->nodes));
  cmd_buffer_upload(cmd, gpu, rm, chunk->res.gpu_palette, chunk->palette.data, vec_bytes_len(&chunk->palette));

  cmd_buffer_upload(cmd, gpu, rm, chunk->res.gpu_child_indices, chunk->child_indices.data,
                    vec_bytes_len(&chunk->child_indices));

  chunk->need_upload = false;
}
// --- Private Functions ---
