#pragma once

// HalfSipHash-2-4 with a 32-bit result (Aumasson and Bernstein; the
// reference is github.com/veorq/SipHash), for hash tables whose keys a peer
// chooses. 32-bit arithmetic only, which suits the ESP32-S3.

#include <cstdint>

namespace prim {

uint32_t halfsiphash(const void* in, uint32_t len, const uint8_t key[8]);

// The key every table uses: set once at boot from the random source. Until
// it is set it is zero, which hashes well but predictably.
void hash_seed(const uint8_t key[8]);
inline uint32_t hash32(const void* in, uint32_t len);

// ---- Implementation ----

namespace detail {

inline uint8_t g_key[8] = {};

inline uint32_t rotl(uint32_t x, int b) { return (x << b) | (x >> (32 - b)); }

inline uint32_t le32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

inline void rounds(uint32_t v[4], int n) {
  while (n--) {
    v[0] += v[1];
    v[1] = rotl(v[1], 5) ^ v[0];
    v[0] = rotl(v[0], 16);
    v[2] += v[3];
    v[3] = rotl(v[3], 8) ^ v[2];
    v[0] += v[3];
    v[3] = rotl(v[3], 7) ^ v[0];
    v[2] += v[1];
    v[1] = rotl(v[1], 13) ^ v[2];
    v[2] = rotl(v[2], 16);
  }
}

}  // namespace detail

inline uint32_t halfsiphash(const void* in, uint32_t len, const uint8_t key[8]) {
  const auto* p = static_cast<const uint8_t*>(in);
  const uint32_t k0 = detail::le32(key), k1 = detail::le32(key + 4);
  uint32_t v[4] = {k0, k1, 0x6c796765u ^ k0, 0x74656462u ^ k1};
  // Every 4 octets is a word; the last word is what is left, with the length on top.
  for (uint32_t i = 0;; i += 4) {
    uint32_t m = len << 24;
    if (len - i >= 4) {
      m = detail::le32(p + i);
    } else {
      for (uint32_t j = len - i; j--;) m |= uint32_t(p[i + j]) << (8 * j);
    }
    v[3] ^= m;
    detail::rounds(v, 2);
    v[0] ^= m;
    if (len - i < 4) break;
  }
  v[2] ^= 0xff;
  detail::rounds(v, 4);
  return v[1] ^ v[3];
}

inline void hash_seed(const uint8_t key[8]) {
  for (int i = 0; i < 8; ++i) detail::g_key[i] = key[i];
}

inline uint32_t hash32(const void* in, uint32_t len) { return halfsiphash(in, len, detail::g_key); }

}  // namespace prim
