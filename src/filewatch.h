// filewatch.h
#pragma once
#include <stdbool.h>

typedef void (*FWCallback)(const char *path, void *user_data);

typedef struct FileWatcher FileWatcher;

// Initialize watcher
FileWatcher *fw_init();

// Watch a specific file or directory
void fw_add_watch(FileWatcher *fw, const char *path, FWCallback cb, void *user_data);

// Poll for changes (call this in your main loop)
void fw_poll(FileWatcher *fw);

// Cleanup
void fw_destroy(FileWatcher *fw);