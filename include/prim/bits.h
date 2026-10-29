#pragma once

// Counting leading zeros, the one thing the library wants from the machine
// that C++ does not give it: arena.h finds a capacity's size class with it
// and trie.h the first bit two keys differ in. The argument must not be zero.
//
// GCC and Clang have a builtin, which is one instruction on a core that has
// CLZ and a small library call on one that does not. IAR has an intrinsic.
// Anything else gets the portable version, which the tests check against the
// builtin for every bit position.

#include <cstdint>

namespace prim {

// Without a builtin: smear the top bit down, then one multiply and a table
// give its position.
inline int clz32_portable(uint32_t x) {
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  static constexpr uint8_t kTop[32] = {0,  9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3,  30,
                                        8,  12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4,  31};
  return 31 - kTop[(x * 0x07C4ACDDu) >> 27];
}
inline int clz64_portable(uint64_t x) {
  const uint32_t hi = uint32_t(x >> 32);
  return hi ? clz32_portable(hi) : 32 + clz32_portable(uint32_t(x));
}

#if defined(__GNUC__) || defined(__clang__)
inline int clz32(uint32_t x) { return __builtin_clz(x); }
inline int clz64(uint64_t x) { return __builtin_clzll(x); }
#elif defined(__ICCARM__)
inline int clz32(uint32_t x) { return int(__CLZ(x)); }
inline int clz64(uint64_t x) { return clz64_portable(x); }
#else
inline int clz32(uint32_t x) { return clz32_portable(x); }
inline int clz64(uint64_t x) { return clz64_portable(x); }
#endif

}  // namespace prim
