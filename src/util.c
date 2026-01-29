#include "util.h"
#include "common.h"
#include "gpu/pipeline.h"
#include "gpu/swapchain.h"

// --- Private Prototypes ---
static const char *vk_result_to_string(VkResult r);

void vk_check(VkResult err) {
  if (err != VK_SUCCESS) {
    LOG_ERROR("VkError: %s", vk_result_to_string(err));

    abort();
  }
}
VkSemaphore vk_create_semp_timeline(VkDevice device, const char *name) {
  VkSemaphoreTypeCreateInfo timelineCreateInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                  .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                                  .initialValue = 0};

  VkSemaphoreCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineCreateInfo};
  VkSemaphore timeline = {};
  vkCreateSemaphore(device, &createInfo, NULL, &timeline);
  vk_set_object_name(device, VK_OBJECT_TYPE_SEMAPHORE, (u64)timeline, name);
  return timeline;
}

void vk_set_image_name(VkDevice device, VkImage image, const char* name){
  vk_set_object_name(device, VK_OBJECT_TYPE_IMAGE, (u64)image, name);
}

VkSemaphore vk_create_semp_binary(VkDevice device, const char *name) {

  VkSemaphoreCreateInfo createInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore timeline = {};
  vkCreateSemaphore(device, &createInfo, NULL, &timeline);
  vk_set_object_name(device, VK_OBJECT_TYPE_SEMAPHORE, (u64)timeline, name);
  return timeline;
}

void vk_set_object_name(VkDevice device, VkObjectType objectType, uint64_t objectHandle, const char *name) {
  if (!name || !name[0])
    return;

  VkDebugUtilsObjectNameInfoEXT info = {0};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectType = objectType;
  info.objectHandle = objectHandle;
  info.pObjectName = name;

  vkSetDebugUtilsObjectNameEXT(device, &info);
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

static const char *vk_result_to_string(VkResult r) {
  switch (r) {
  case VK_SUCCESS:
    return "VK_SUCCESS";
  case VK_NOT_READY:
    return "VK_NOT_READY";
  case VK_TIMEOUT:
    return "VK_TIMEOUT";
  case VK_EVENT_SET:
    return "VK_EVENT_SET";
  case VK_EVENT_RESET:
    return "VK_EVENT_RESET";
  case VK_INCOMPLETE:
    return "VK_INCOMPLETE";

  case VK_ERROR_OUT_OF_HOST_MEMORY:
    return "VK_ERROR_OUT_OF_HOST_MEMORY";
  case VK_ERROR_OUT_OF_DEVICE_MEMORY:
    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
  case VK_ERROR_INITIALIZATION_FAILED:
    return "VK_ERROR_INITIALIZATION_FAILED";
  case VK_ERROR_DEVICE_LOST:
    return "VK_ERROR_DEVICE_LOST";
  case VK_ERROR_MEMORY_MAP_FAILED:
    return "VK_ERROR_MEMORY_MAP_FAILED";
  case VK_ERROR_LAYER_NOT_PRESENT:
    return "VK_ERROR_LAYER_NOT_PRESENT";
  case VK_ERROR_EXTENSION_NOT_PRESENT:
    return "VK_ERROR_EXTENSION_NOT_PRESENT";
  case VK_ERROR_FEATURE_NOT_PRESENT:
    return "VK_ERROR_FEATURE_NOT_PRESENT";
  case VK_ERROR_INCOMPATIBLE_DRIVER:
    return "VK_ERROR_INCOMPATIBLE_DRIVER";
  case VK_ERROR_TOO_MANY_OBJECTS:
    return "VK_ERROR_TOO_MANY_OBJECTS";
  case VK_ERROR_FORMAT_NOT_SUPPORTED:
    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
  case VK_ERROR_FRAGMENTED_POOL:
    return "VK_ERROR_FRAGMENTED_POOL";
  case VK_ERROR_OUT_OF_POOL_MEMORY:
    return "VK_ERROR_OUT_OF_POOL_MEMORY";
  case VK_ERROR_INVALID_EXTERNAL_HANDLE:
    return "VK_ERROR_INVALID_EXTERNAL_HANDLE";

  case VK_ERROR_SURFACE_LOST_KHR:
    return "VK_ERROR_SURFACE_LOST_KHR";
  case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
    return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
  case VK_SUBOPTIMAL_KHR:
    return "VK_SUBOPTIMAL_KHR";
  case VK_ERROR_OUT_OF_DATE_KHR:
    return "VK_ERROR_OUT_OF_DATE_KHR";
  case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
    return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
  case VK_ERROR_VALIDATION_FAILED_EXT:
    return "VK_ERROR_VALIDATION_FAILED_EXT";

  case VK_ERROR_INVALID_SHADER_NV:
    return "VK_ERROR_INVALID_SHADER_NV";
  case VK_ERROR_FRAGMENTATION_EXT:
    return "VK_ERROR_FRAGMENTATION_EXT";
  case VK_ERROR_NOT_PERMITTED_EXT:
    return "VK_ERROR_NOT_PERMITTED_EXT";

#ifdef VK_ERROR_UNKNOWN
  case VK_ERROR_UNKNOWN:
    return "VK_ERROR_UNKNOWN";
#endif

  default:
    return "VK_RESULT_UNKNOWN";
  }
}
