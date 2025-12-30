// filewatch.h
#pragma once
#include "vector.h"
#include <stdbool.h>

#define INVALID_INDEX UINT32_MAX
#define INVALID_HANDLE                                                         \
  (FileHandle) { INVALID_INDEX, INVALID_INDEX }

typedef struct {
  uint32_t index;   // Slot in the manager
  uint32_t version; // Version of the file when handle was issued
} FileHandle;

typedef struct FileManager FileManager;

// PUBLIC FUNCTIONS

bool fm_is_old_version(FileManager *fm, FileHandle handle);

FileHandle fm_update_handle(FileManager *fm, FileHandle handle);
FileHandle fm_load(FileManager *fm, const char *path);
bool fm_is_modified(FileManager *fm, FileHandle handle);
const char *fm_get_source(FileManager *fm, FileHandle *handle);

void fm_poll(FileManager *fm);

// Regular no state reading files
bool file_write_binary(const char *path, const void *data, size_t size);
Vector file_read_binary(const char *path);
