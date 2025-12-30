#include "filewatch.h"
#include "vector.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
  uint32_t version;
} FileEntry;

typedef struct FileManager {
  uint64_t dirty_mask;

  VECTOR_TYPES(FileEntry)
  Vector entries;
} FileManager;

typedef struct SubManager {
  FileManager *fm;
  uint64_t care_mask;
} FileGroup;

// --- Private Prototypes ---
static const char *_get_source(FileManager *fm, FileHandle *handle);
static FileHandle _load_file(FileManager *fm, const char *path);
static time_t get_file_time(const char *path);

// --- Core API ---

FileManager *fm_init() {
  FileManager *fm = calloc(sizeof(FileManager), 1);
  vec_init(&fm->entries, sizeof(FileEntry), NULL);
  return fm;
}
void fm_poll(FileManager *fm) {
  fm->dirty_mask = 0; // Reset per-frame mask
  FileEntry *entries = (FileEntry *)fm->entries.data;

  for (size_t i = 0; i < fm->entries.length; i++) {
    FileEntry *e = &entries[i];
    time_t disk_time = get_file_time(e->path);

    if (disk_time != 0 && disk_time != e->last_mod) {
      Vector next = file_read_binary(e->path);
      if (next.data) {
        free(e->source);
        e->source = (char *)next.data;
        e->size = next.length;
        e->last_mod = disk_time;
        e->version++;

        if (i < 64)
          fm->dirty_mask |= (1ULL << i);
        LOG_INFO("Hot-Reload: %s (v%d)", e->path, e->version);
      }
    }
  }
}

// --- SubManager API ---

FileGroup *fg_init(FileManager *fm) {
  FileGroup *fg = calloc(sizeof(FileGroup), 1);
  fg->fm = fm;
  return fg;
}

bool fg_is_modified(FileGroup *sm) {
  return (sm->fm->dirty_mask & sm->care_mask) != 0;
}

FileHandle fg_load_file(FileGroup *sm, const char *path) {
  FileHandle handle = _load_file(sm->fm, path);
  sm->care_mask |= 1 << handle.index;
  return handle;
}

const char *fg_get_file(FileGroup *sm, FileHandle *handle) {
  sm->care_mask |= 1 << handle->index;
  return _get_source(sm->fm, handle);
}

void fg_reset(FileGroup *sm) { sm->care_mask = 0; }

bool file_write_binary(const char *path, const void *data, size_t size) {
  if (!path || !data)
    return false;
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  size_t written = fwrite(data, 1, size, f);
  fclose(f);
  return written == size;
}

Vector file_read_binary(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return (Vector){0};

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  Vector vec = {0};
  vec_init_with_capacity(&vec, size + 1, 1, NULL);
  fread(vec.data, 1, size, f);
  ((char *)vec.data)[size] = '\0'; // Null terminate for safety
  vec.length = size;
  fclose(f);
  return vec;
}

// --- Private Functions ---

// --- Getters & Helpers ---
static const char *_get_source(FileManager *fm, FileHandle *handle) {
  if (handle->index == INVALID_INDEX || handle->index > fm->entries.length)
    return NULL;
  return ((FileEntry *)fm->entries.data)[handle->index].source;
}

static FileHandle _load_file(FileManager *fm, const char *path) {
  if (!fm || !path)
    return INVALID_HANDLE;

  FileEntry *entries = (FileEntry *)fm->entries.data;
  for (uint32_t i = 0; i < (uint32_t)fm->entries.length; i++) {
    if (strcmp(entries[i].path, path) == 0) {
      return (FileHandle){.index = i};
    }
  }

  Vector content = file_read_binary(path);
  if (!content.data)
    return INVALID_HANDLE;

  FileEntry e = {.path = strdup(path),
                 .source = (char *)content.data,
                 .size = content.length,
                 .last_mod = get_file_time(path),
                 .version = 1};

  vec_push(&fm->entries, &e);
  return (FileHandle){.index = (uint32_t)fm->entries.length - 1};
}

static time_t get_file_time(const char *path) {
  stat_struct s;
  if (stat_func(path, &s) == 0)
    return s.st_mtime;
  return 0;
}
