#include "common.h"

/** @brief Typedef for a system initialize function pointer. */
typedef bool (*PFN_system_initialize)(void *config, u32 *memory_len);
/** @brief Typedef for a system shutdown function pointer. */
typedef void (*PFN_system_shutdown)();
/** @brief Typedef for a update function pointer. */
typedef bool (*PFN_system_update)();

typedef struct SystemFunc {
  PFN_system_initialize on_init;
  PFN_system_update on_update;
  PFN_system_shutdown on_shutdown;
} SystemFunc;

// PUBLIC FUNCTIONS
void m_system_register(SystemFunc func, SystemType type, void *config);
bool m_system_init();
void m_system_update();
void *m_system_get(SystemType type);

#define _CHECK_TYPE_MATCH(enum_id, type_struct)                                                                        \
  static_assert(_ID_##type_struct == enum_id,                                                                          \
                "CRITICAL ERROR: You are casting '" #type_struct "' using the wrong System ID!");

#if ASSERT_DEBUG == true
// DEBUG: Check types, then fetch pointer
#define SYSTEM_GET(enum_id, type_struct)                                                                               \
  ((type_struct *)m_system_get(enum_id));                                                                              \
  _CHECK_TYPE_MATCH(enum_id, type_struct);                                                                             \
  {                                                                                                                    \
    int ____macro = enum_id;                                                                                           \
  }
#else
// RELEASE: Just fetch pointer
#define SYSTEM_GET(enum_id, type_struct) (type_struct *)m_system_get[enum_id]
#endif
#define SYSTEM_HELPER_MEM(mem, type)                                                                                   \
  if (*mem == 0) {                                                                                                     \
    *mem = sizeof(type);                                                                                               \
    return false;                                                                                                      \
  }
