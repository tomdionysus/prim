#pragma once

// The tests' harness, and what every test shares: an arena to allocate from
// and an allocator that fails on demand. The tests may use std:: containers
// as oracles; the library itself does not.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "prim/arena.h"

namespace check {

using Fn = void (*)();
struct Case {
  const char* name;
  Fn fn;
  Case* next;
};
inline Case*& head() {
  static Case* h = nullptr;
  return h;
}
inline int& failures() {
  static int f = 0;
  return f;
}
struct Reg {
  Case c;
  Reg(const char* name, Fn fn) : c{name, fn, head()} { head() = &c; }
};
inline void fail(const char* file, int line, const char* expr) {
  std::printf("  FAIL %s:%d  %s\n", file, line, expr);
  ++failures();
}

// A seeded generator; PRIM_SEED moves every random test.
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t salt) {
    const char* e = std::getenv("PRIM_SEED");
    s = (e ? std::strtoull(e, nullptr, 0) : 0x9E3779B97F4A7C15ull) ^ salt;
    if (!s) s = 1;
  }
  uint32_t operator()() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return uint32_t(s >> 11);
  }
  uint32_t below(uint32_t n) { return (*this)() % n; }
};

// An arena over a buffer of its own, from malloc so that it is not on the
// stack: on the S3 a task's stack is a few KB.
#ifndef PRIM_TEST_HEAP
#define PRIM_TEST_HEAP (1u << 18)
#endif
struct Buffer {
  void* raw;
  char* buf;
  explicit Buffer(uint32_t len) : raw(std::malloc(len + 64)), buf(static_cast<char*>(raw) + (64 - reinterpret_cast<uintptr_t>(raw) % 64)) {}
  ~Buffer() { std::free(raw); }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
};
struct Heap : Buffer {
  prim::Arena a;
  explicit Heap(uint32_t len = PRIM_TEST_HEAP) : Buffer(len) { a.add(buf, len, prim::Mem::Fast); }  // as a V3 has it
};

// Fails the n-th call to alloc or resize (counting from 0), or every call
// from then on.
struct Failing final : prim::Alloc {
  prim::Alloc& to;
  int fail_at;
  bool from_then_on;
  int calls = 0;
  explicit Failing(prim::Alloc& a, int at = -1, bool on = false) : to(a), fail_at(at), from_then_on(on) {}
  bool fails() {
    const int c = calls++;
    return fail_at >= 0 && (c == fail_at || (from_then_on && c > fail_at));
  }
  void* alloc(uint32_t n, prim::Mem want = prim::Mem::Bulk) override { return fails() ? nullptr : to.alloc(n, want); }
  void* resize(void* p, uint32_t n) override { return fails() ? nullptr : to.resize(p, n); }
  void free(void* p) override { to.free(p); }
  uint32_t size_of(const void* p) const override { return to.size_of(p); }
  prim::Stats stats(prim::Mem kind) const override { return to.stats(kind); }
};

}  // namespace check

#define TEST(name)                                     \
  static void name();                                  \
  static ::check::Reg check_reg_##name(#name, name);   \
  static void name()

#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) ::check::fail(__FILE__, __LINE__, #cond);   \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
