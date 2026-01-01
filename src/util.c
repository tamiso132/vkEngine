#include "util.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/swapchain.h"
#include "resmanager.h"

// --- Private Prototypes ---

void vk_check(VkResult err) {
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
char *str_sub(const char *s, int start, int len) {
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
char *str_get_dir(const char *path) {
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

// --- Private Functions ---
