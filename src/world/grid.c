#include "common.h"
#include "hashmaputil.h"
#include "vector.h"

typedef struct HashItem {
  u32 item;
} HashItem;

HM_TYPED_U64KEY(hm_grid_slot, HashItem, item);
