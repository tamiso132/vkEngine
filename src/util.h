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
#define NOT_IMPLEMENTED() {LOG_MESSAGE(CLR_RED, "ERROR", "NOT IMPLEMENTED"); abort();}

#define defer(end) for (int _i = 0; _i == 0; (_i = 1), end)

// FOR DEFERRING
typedef struct {
  void *p;
  void (*fn)(void *);
} _defer_guard;

static inline void _defer_cleanup(_defer_guard *g) {
  if (g->fn && g->p)
    g->fn(g->p);
}

#define _DEFER_JOIN2(a, b) a##b
#define _DEFER_JOIN(a, b) _DEFER_JOIN2(a, b)

// Generic: run `fn(ptr)` at scope exit
#define DEFER_PTR(ptr_expr, fn_expr)                                                                                   \
  __attribute__((cleanup(_defer_cleanup))) _defer_guard _DEFER_JOIN(_defer_, __COUNTER__) = {                          \
      (void *)(ptr_expr), (void (*)(void *))(fn_expr)}

// Convenience: assume free(ptr)
#define DEFER_FREE_PTR(ptr_expr) DEFER_PTR((ptr_expr), free)
#define DEFER_VEC(ptr_expr) DEFER_PTR((ptr_expr), vec_free)

// PUBLIC FUNCTIONS
void vk_check(VkResult err);
VkSemaphore vk_create_semp_binary(VkDevice device, const char *name);
VkSemaphore vk_create_semp_timeline(VkDevice device, const char *name);
void vk_set_image_name(VkDevice device, VkImage image, const char* name);
void vk_set_object_name(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char *name);
char *str_get_dir(const char *path);
char *str_sub(const char *s, int start, int len);

/** * Extract directory from path (Non-destructive) * Example: "src/main.c" -> returns "src/" */ char *
str_get_dir(const char *path);

// END PUBLIC FUNCTIONS
