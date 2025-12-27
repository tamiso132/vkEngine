// filewatch.h
#pragma once
#include <stdbool.h>

typedef void (*FWCallback)(const char *path, void *user_data);

typedef struct FileWatcher FileWatcher;

// PUBLIC FUNCTIONS
FileWatcher *fw_init();
void fw_poll(FileWatcher *fw);
void fw_destroy(FileWatcher *fw);
void fw_add_watch(FileWatcher *fw, const char *path, FWCallback cb,
                  void *user_data);
