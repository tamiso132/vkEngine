#include "filewatch.h"
#include "vector.h"
#include <stdint.h>
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
  char *source;
  size_t size;
  time_t last_mod;
  uint32_t version; // Incremented on every disk change
} FileEntry;

typedef struct FileManager {
  FileEntry *entries;
  size_t count;
  size_t capacity;
} FileManager;

// --- Private Prototypes ---
static void _update_handle(FileManager *fm, FileHandle *handle);
static time_t get_file_time(const char *path);

bool file_write_binary(const char *path, const void *data, size_t size) {
  if (!path || !data)
    return false;

  FILE *f = fopen(path, "wb"); // 'wb' is critical for binary data
  if (!f) {
    printf("Error: Failed to open file for writing: %s\n", path);
    return false;
  }

  size_t written = fwrite(data, 1, size, f);
  fclose(f);

  if (written != size) {
    printf("Error: Failed to write all bytes to %s (wrote %zu of %zu)\n", path,
           written, size);
    return false;
  }

  return true;
}
Vector file_read_binary(const char *path) {
  FILE *f = fopen(path, "rb");

  if (!f)
    return (Vector){};

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  Vector vec = {};
  vec_init_with_capacity(&vec, size + 1, 1, NULL);
  fread(vec.data, 1, size, f);
  vec.length = size;
  fclose(f);

  return vec;
}

void fm_poll(FileManager *fm) {
  for (size_t i = 0; i < fm->count; i++) {
    FileEntry *e = &fm->entries[i];
    time_t disk_time = get_file_time(e->path);

    if (disk_time != 0 && disk_time != e->last_mod) {
      Vector next = file_read_binary(e->path);
      if (next.data) {
        free(e->source);
        e->source = (char *)next.data;
        e->size = next.length;
        e->last_mod = disk_time;
        e->version++; // Handle's version is now "older" than this

        LOG_INFO("Hot-Reload: %s (v%d)", e->path, e->version);
      }
    }
  }
}

bool fm_is_old_version(FileManager *fm, FileHandle handle) {
  if (handle.index == 0 || handle.index > fm->count)
    return false;

  FileEntry *e = &fm->entries[handle.index - 1];
  return e->version > handle.version;
}

const char *fm_get_source(FileManager *fm, FileHandle *handle) {
  if (handle->index == 0 || handle->index > fm->count)
    return NULL;

  _update_handle(fm, handle);
  return fm->entries[handle->index - 1].source;
}

// --- Private Functions ---

// Updates a handle to the latest version (clears "is_modified" status)
static void _update_handle(FileManager *fm, FileHandle *handle) {

  handle->version = fm->entries[handle->index - 1].version;
}

static time_t get_file_time(const char *path) {
  stat_struct s;
  if (stat_func(path, &s) == 0) {
    return s.st_mtime;
  }
  return 0;
}
