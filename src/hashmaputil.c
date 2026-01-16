

#include "hashmap.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// --- Private Prototypes ---

uint64_t hm_mix_u64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

inline uint64_t hm_pack_i21(int32_t v) {
  // bias signed -> unsigned
  return (uint64_t)(v + (1 << 20)) & ((1ull << 21) - 1);
}

inline uint64_t hm_pack_vec3_i21(int32_t x, int32_t y, int32_t z) {
  return (hm_pack_i21(x) << 42) | (hm_pack_i21(y) << 21) | hm_pack_i21(z);
}
// --- Private Functions ---
