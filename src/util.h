#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <volk.h>

typedef uint8_t u8;
typedef int8_t i8;

typedef float f32;
typedef double f64;

typedef int32_t i32;
typedef int64_t i64;

typedef uint32_t u32;
typedef uint64_t u64;

// LOGGING
#define CLR_RESET "\033[0m"
#define CLR_RED "\033[0;31m"
#define CLR_YLW "\033[0;33m"
#define CLR_CYN "\033[0;36m"
#define CLR_GRY "\033[0;90m"

// Use &__FILE__[offset] instead of __FILE__ + offset
#define RELATIVE_FILE                                                          \
  (strncmp(__FILE__, PROJECT_ROOT, sizeof(PROJECT_ROOT) - 1) == 0              \
       ? &__FILE__[sizeof(PROJECT_ROOT) - 1]                                   \
       : __FILE__)

#define LOG_MESSAGE(color, label, fmt, ...)                                    \
  do {                                                                         \
    fprintf(stdout,                                                            \
            CLR_GRY "%s:%d " CLR_RESET color "%-5s" CLR_RESET " " fmt "\n",    \
            RELATIVE_FILE, __LINE__, label, ##__VA_ARGS__);                    \
  } while (0)
#define LOG_TRACE(fmt, ...) LOG_MESSAGE(CLR_GRY, "TRACE", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_MESSAGE(CLR_CYN, "INFO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_MESSAGE(CLR_YLW, "WARN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_MESSAGE(CLR_RED, "ERROR", fmt, ##__VA_ARGS__)

static inline void vk_check(VkResult err) {
  if (err != VK_SUCCESS) {
    LOG_ERROR("VkError: %d", err);

    abort();
  }
}
/**
 * Returns a new heap-allocated substring.
 * start: index to begin at
 * len: number of characters to copy
 */
static inline char *str_sub(const char *s, int start, int len) {
  if (!s || strlen(s) < start)
    return NULL;

  char *sub = malloc(len + 1);
  if (!sub)
    return NULL;

  memcpy(sub, s + start, len);
  sub[len] = '\0';
  return sub;
}

/**
 * Extract directory from path (Non-destructive)
 * Example: "src/main.c" -> returns "src/"
 */
static inline char *str_get_dir(const char *path) {
  char *last_slash = strrchr(path, '/');
#ifdef _WIN32
  char *last_back = strrchr(path, '\\');
  if (last_back > last_slash)
    last_slash = last_back;
#endif

  if (!last_slash)
    return strdup("");

  int len = (int)(last_slash - path) + 1;
  return str_sub(path, 0, len);
}
