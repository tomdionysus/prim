#pragma once

// Clearing memory that held a secret. The arena does not clear what is
// freed: a block keeps its contents, past the free-list links written into
// its first words, until it is allocated and overwritten. A key, a
// plaintext or a password is cleared by its owner before it is freed, with
// secure_zero, which the compiler cannot remove as a dead store.

#include <cstdint>

namespace prim {

inline void secure_zero(void* p, uint32_t n) {
  volatile uint8_t* q = static_cast<volatile uint8_t*>(p);
  while (n--) *q++ = 0;
}

}  // namespace prim
