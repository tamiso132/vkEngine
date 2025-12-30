#include "util.h"

// --- Private Prototypes ---

CmdBuffer cmd_init(VkDevice device, u32 queue_fam) {
  CmdBuffer cmd = {};
  VkCommandPoolCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = queue_fam};

  vk_check(vkCreateCommandPool(device, &info, NULL, &cmd.pool));
  VkCommandBufferAllocateInfo allocInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandPool = cmd.pool,
      .commandBufferCount = 1};

  vk_check(vkAllocateCommandBuffers(device, &allocInfo, &cmd.buffer));
  return cmd;
}

// Helper to start a one-time command buffer
void cmd_begin(VkDevice device, CmdBuffer cmd) {

  VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

  vk_check(vkBeginCommandBuffer(cmd.buffer, &beginInfo));
}

// Helper to end and flush the command buffer immediately
void cmd_end(VkDevice device, CmdBuffer cmd) {
  vk_check(vkEndCommandBuffer(cmd.buffer));
}

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
