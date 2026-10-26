#pragma once

// Fixed-size objects from slabs. A slab is one allocation of a header and
// per_slab objects; a free object holds a 16-bit offset to the next free one,
// so the pool costs nothing per object and two words per slab. A slab whose
// objects are all back goes back to the allocator, unless it is the only
// one; a slab something comes back to moves to the front, so the next get
// finds it first. Objects made and freed all the time come from pools, so
// their churn does not fragment the arena.

#include <cstdint>

#include "prim/alloc.h"

namespace prim {

class Pool {
 public:
  // size is rounded up to 8 and to at least a pointer; a slab must fit 16-bit
  // offsets, so per_slab is cut to what does.
  Pool(Alloc& a, uint32_t size, uint32_t per_slab);
  ~Pool();  // frees every slab, whatever is still out
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  void* get();        // nullptr when out of memory
  bool put(void* p);  // false, and nothing done, if p is not an object of this pool
  uint32_t in_use() const;
  uint32_t slabs() const;
  uint32_t size() const { return size_; }
  uint32_t per_slab() const { return per_; }

 private:
  // Objects start a multiple of 8 into the slab, so they are aligned as the arena aligns the slab.
  static constexpr uint32_t kHeader = (sizeof(void*) + 2 * sizeof(uint16_t) + 7) & ~7u;
  static uint16_t& link(void* slab, uint32_t off) {
    return *reinterpret_cast<uint16_t*>(static_cast<char*>(slab) + off);
  }

  struct Slab {
    Slab* next;
    uint16_t free;  // the first free object's offset from the slab; 0 when none
    uint16_t used;
  };
  Alloc* a_;
  Slab* s_ = nullptr;
  uint16_t size_;
  uint16_t per_;
};

// ---- Implementation ----

inline Pool::Pool(Alloc& a, uint32_t size, uint32_t per_slab) : a_(&a) {
  // Rounded, then cut to the largest object a slab of one can hold.
  size = ((size < sizeof(void*) ? sizeof(void*) : size) + 7) & ~7u;
  const uint32_t top = (0xFFFFu - kHeader) & ~7u;
  size = size > top ? top : size;
  const uint32_t most = (0xFFFFu - kHeader) / size;
  per_slab = per_slab ? per_slab : 1;
  size_ = uint16_t(size);
  per_ = uint16_t(per_slab > most ? most : per_slab);
}

inline Pool::~Pool() {
  while (Slab* s = s_) {
    s_ = s->next;
    a_->free(s);
  }
}

inline void* Pool::get() {
  Slab* s = s_;
  while (s && !s->free) s = s->next;
  if (!s) {
    s = static_cast<Slab*>(a_->alloc(kHeader + per_ * size_));
    if (!s) return nullptr;
    // Every object onto the slab's free list, the first at its head.
    uint32_t next = 0;
    for (uint32_t i = per_; i--;) {
      const uint32_t o = kHeader + i * size_;
      link(s, o) = uint16_t(next);
      next = o;
    }
    s->free = uint16_t(next);
    s->used = 0;
    s->next = s_;
    s_ = s;
  }
  const uint32_t o = s->free;
  s->free = link(s, o);
  ++s->used;
  return reinterpret_cast<char*>(s) + o;
}

inline bool Pool::put(void* p) {
  Slab** at = &s_;
  for (; *at; at = &(*at)->next) {
    const uintptr_t off = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(*at) - kHeader;
    if (off < uintptr_t(per_) * size_ && off % size_ == 0) break;
  }
  Slab* s = *at;
  if (!s) return false;
  const uint32_t o = uint32_t(reinterpret_cast<char*>(p) - reinterpret_cast<char*>(s));
  link(s, o) = s->free;
  s->free = uint16_t(o);
  if (--s->used == 0 && (s != s_ || s->next)) {
    *at = s->next;
    a_->free(s);
  } else if (s != s_) {
    *at = s->next;
    s->next = s_;
    s_ = s;
  }
  return true;
}

inline uint32_t Pool::in_use() const {
  uint32_t n = 0;
  for (const Slab* s = s_; s; s = s->next) n += s->used;
  return n;
}

inline uint32_t Pool::slabs() const {
  uint32_t n = 0;
  for (const Slab* s = s_; s; s = s->next) ++n;
  return n;
}

}  // namespace prim
