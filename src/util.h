#pragma once
#include "cglm/types.h"
#include "common.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <volk.h>

// LOGGING
#define CLR_RESET "\033[0m"
#define CLR_RED "\033[0;31m"
#define CLR_YLW "\033[0;33m"
#define CLR_CYN "\033[0;36m"
#define CLR_GRY "\033[0;90m"

// Use &__FILE__[offset] instead of __FILE__ + offset
#define RELATIVE_FILE                                                                                                  \
  (strncmp(__FILE__, PROJECT_ROOT, sizeof(PROJECT_ROOT) - 1) == 0 ? &__FILE__[sizeof(PROJECT_ROOT) - 1] : __FILE__)

#define LOG_MESSAGE(color, label, fmt, ...)                                                                            \
  do {                                                                                                                 \
    fprintf(stdout, CLR_GRY "%s:%d " CLR_RESET color "%-5s" CLR_RESET " " fmt "\n", RELATIVE_FILE, __LINE__, label,    \
            ##__VA_ARGS__);                                                                                            \
  } while (0)

#define LOG_TRACE(fmt, ...) LOG_MESSAGE(CLR_GRY, "TRACE", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_MESSAGE(CLR_CYN, "INFO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_MESSAGE(CLR_YLW, "WARN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_MESSAGE(CLR_RED, "ERROR", fmt, ##__VA_ARGS__)

typedef struct CmdBuffer {
  VkCommandPool pool;
  VkCommandBuffer buffer;
} CmdBuffer;

typedef struct RenderingBeginInfo {
  ResHandle *colors;
  u32 colors_count;

  VkAttachmentLoadOp loadOp;
  VkAttachmentStoreOp storeOp;

  vec3 clear_color;
  u32 w;
  u32 h;

} RenderingBeginInfo;

typedef struct BindPipelineInfo {
  PipelineHandle handle;
  void *p_push;
  u32 push_size;

} BindPipelineInfo;

// PUBLIC FUNCTIONS

CmdBuffer cmd_init(VkDevice device, u32 queue_fam);

void cmd_begin(VkDevice device, CmdBuffer cmd);
void cmd_end(VkDevice device, CmdBuffer cmd);

void cmd_begin_rendering(CmdBuffer cmd, ResourceManager *rm, RenderingBeginInfo *info);
void cmd_end_rendering(CmdBuffer cmd);

void cmd_bind_bindless(CmdBuffer cmd, ResourceManager *rm, VkExtent2D extent);
void cmd_bind_pipeline(CmdBuffer cmd, M_Pipeline *pm, BindPipelineInfo *info);

void vk_check(VkResult err);

/** * Returns a new heap-allocated substring. * start: index to begin at * len: number of characters to copy */
char *str_sub(const char *s, int start, int len);

/** * Extract directory from path (Non-destructive) * Example: "src/main.c" -> returns "src/" */ char *
str_get_dir(const char *path);
