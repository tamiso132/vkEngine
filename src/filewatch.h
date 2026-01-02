// filewatch.h
#pragma once
#include "vector.h"
#include <stdbool.h>

#define INVALID_INDEX UINT32_MAX
#define INVALID_HANDLE                                                                                                 \
  (FileHandle) { INVALID_INDEX }

typedef struct {
  uint32_t index; // Slot in the manager
} FileHandle;

typedef struct M_File M_File;
typedef struct SubManager FileGroup;

// PUBLIC FUNCTIONS

void fm_poll(M_File *fm);
M_File *fm_init();

FileGroup *fg_init(M_File *fm);
bool fg_is_modified(FileGroup *sm);
FileHandle fg_load_file(FileGroup *sm, const char *path);
const char *fg_get_file(FileGroup *sm, FileHandle *handle);
void fg_reset(FileGroup *sm);

bool file_write_binary(const char *path, const void *data, size_t size);
Vector file_read_binary(const char *path);
