#include "system_manager.h"
#include <assert.h>
#include <stdlib.h>

typedef struct SystemCtx {
  SystemFunc func;
  bool is_registered;
  void *self;
  void *config;
} SystemCtx;

typedef struct SystemManager {
  SystemCtx systems[SYSTEM_TYPE_COUNT];
} SystemManager;

// --- Private Prototypes ---

static SystemManager _managers = {};

bool m_system_init() {
  for (u32 i = 0; i < SYSTEM_TYPE_COUNT; i++) {
    SystemCtx *system = &_managers.systems[i];
    if (system->is_registered && system->func.on_init) {
      u32 mem_req = 0;
      system->func.on_init(system->config, &mem_req);
      system->self = calloc(1, mem_req);

      // TODO: Print which one failed later
      if (!system->func.on_init(system->config, &mem_req))
        return false;
    }
  }
  return true;
}

void m_system_register(SystemFunc func, SystemType type, void *config) {
  _managers.systems[type].func = func;
  _managers.systems[type].is_registered = true;
  _managers.systems[type].config = config;
}

void *m_system_get(SystemType type) {
  assert(_managers.systems[type].is_registered);
  return _managers.systems[type].self;
}

void m_system_update() {
  for (u32 i = 0; i < SYSTEM_TYPE_COUNT; i++) {
    SystemCtx *system = &_managers.systems[i];
    if (system->func.on_update)
      system->func.on_update();
  }
}
// --- Private Functions ---
