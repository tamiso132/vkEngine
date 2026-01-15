#include "chunk_internal.h"
#include  "resmanager.h"
#include "command.h"

void chunk_gpu_init(ChunkTree *chunk, M_Resource *rm) {
  RGBufferInfo node_info = {
    .name = "NodeBuffer",
    .capacity = vec_bytes_len(&chunk->nodes),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  };

  RGBufferInfo child_info = {
    .name = "childIndexBuffer",
    .capacity = vec_bytes_len(&chunk->child_indices),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  };

  RGBufferInfo pal_info = {
    .name = "Palette",
    .capacity = vec_bytes_len(&chunk->palette),
    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    .mem = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  };

  chunk->gpu_child_indices = rm_create_buffer(rm, &child_info);
  chunk->gpu_node         = rm_create_buffer(rm, &node_info);
  chunk->gpu_palette      = rm_create_buffer(rm, &pal_info);

  // After creating GPU buffers, you likely want an upload
  chunk->need_upload = true;
}

void chunk_upload(ChunkTree *chunk, M_GPU *gpu, M_Resource *rm, CmdBuffer cmd) {
  if (!chunk->need_upload)
    return;

  cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_node, chunk->nodes.data, vec_bytes_len(&chunk->nodes));
  cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_palette, chunk->palette.data, vec_bytes_len(&chunk->palette));

  cmd_buffer_upload(cmd, gpu, rm, chunk->gpu_child_indices, chunk->child_indices.data,
                    vec_bytes_len(&chunk->child_indices));

  chunk->need_upload = false;
}
