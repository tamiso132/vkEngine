#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

// Grundläggande makro-motor
#define LOG_MESSAGE(level, color, label, fmt, ...)                             \
  do {                                                                         \
    time_t t = time(NULL);                                                     \
    struct tm *tm_info = localtime(&t);                                        \
    char time_buf[10];                                                         \
    strftime(time_buf, 10, "%H:%M:%S", tm_info);                               \
    fprintf(stderr,                                                            \
            "%s %s%-5s" CLR_RESET " " CLR_GRY "%s:%d:" CLR_RESET " " fmt "\n", \
            time_buf, color, label, __FILE__, __LINE__, ##__VA_ARGS__);        \
  } while (0)

// Användarvänliga makron
#define LOG_TRACE(fmt, ...)                                                    \
  LOG_MESSAGE(LOG_TRACE, CLR_GRY, "TRACE", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)                                                     \
  LOG_MESSAGE(LOG_INFO, CLR_CYN, "INFO", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)                                                     \
  LOG_MESSAGE(LOG_WARN, CLR_YLW, "WARN", fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)                                                    \
  LOG_MESSAGE(LOG_ERROR, CLR_RED, "ERROR", fmt, ##__VA_ARGS__)

inline void vk_check(VkResult err) {
  if (err != VK_SUCCESS) {
    LOG_ERROR("VkError: %d", err);
    abort();
  }
}
