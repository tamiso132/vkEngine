#include "filewatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // POSIX standard for file stats
#include <time.h>

// Use standard 'stat' for timestamps
#ifdef _WIN32
#define stat_func _stat
#define stat_struct struct _stat
#else
#define stat_func stat
#define stat_struct struct stat
#endif

typedef struct {
  char *path;
  time_t last_mod;
  FWCallback callback;
  void *user_data;
} WatchEntry;

typedef struct {
  WatchEntry *data;
  size_t count;
  size_t capacity;
} WatchList;

struct FileWatcher {
  WatchList watches;
};

// --- Private Prototypes ---
static time_t get_file_time(const char *path);

FileWatcher *fw_init() {
  FileWatcher *fw = calloc(1, sizeof(FileWatcher));
  // Initial capacity
  fw->watches.capacity = 16;
  fw->watches.data = malloc(fw->watches.capacity * sizeof(WatchEntry));
  return fw;
}

void fw_add_watch(FileWatcher *fw, const char *path, FWCallback cb,
                  void *user_data) {
  if (!fw || !path || !cb)
    return;

  // Check if we need to grow
  if (fw->watches.count >= fw->watches.capacity) {
    fw->watches.capacity *= 2;
    fw->watches.data =
        realloc(fw->watches.data, fw->watches.capacity * sizeof(WatchEntry));
  }

  // Create Entry
  WatchEntry entry;
  entry.path = strdup(path); // Copy string so we own it
  entry.callback = cb;
  entry.user_data = user_data;

  // Record initial time so we don't trigger immediately
  entry.last_mod = get_file_time(path);
  if (entry.last_mod == 0) {
    printf("[FileWatch] Warning: Could not find file %s\n", path);
  }

  fw->watches.data[fw->watches.count++] = entry;
  printf("[FileWatch] Watching: %s\n", path);
}

void fw_poll(FileWatcher *fw) {
  if (!fw)
    return;

  for (size_t i = 0; i < fw->watches.count; i++) {
    WatchEntry *e = &fw->watches.data[i];

    time_t current = get_file_time(e->path);

    // If file exists (time > 0) AND time has changed
    if (current != 0 && current != e->last_mod) {
      // Update timestamp first to avoid loop triggers
      e->last_mod = current;

      // Trigger Callback
      e->callback(e->path, e->user_data);
    }
  }
}

void fw_destroy(FileWatcher *fw) {
  if (!fw)
    return;

  for (size_t i = 0; i < fw->watches.count; i++) {
    free(fw->watches.data[i].path);
  }
  free(fw->watches.data);
  free(fw);
}

// --- Private Functions ---
static time_t get_file_time(const char *path) {
  stat_struct s;
  if (stat_func(path, &s) == 0) {
    return s.st_mtime;
  }
  return 0;
}
