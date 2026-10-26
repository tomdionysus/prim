#pragma once

// Fixed-size objects from slabs. A slab is one allocation of a header, a bit
// for each object, set while it is out, and per_slab objects; a free object
// holds a 16-bit offset to the next free one, so the pool costs nothing per
// object and two words and a bit an object per slab. A slab whose objects
// are all back goes back to the allocator, unless it is the only one; a slab
// something comes back to moves to the front, so the next get finds it
// first. Objects made and freed all the time come from pools, so their churn
// does not fragment the arena.
//
// A pool is an Alloc too, for anything that allocates objects of one size:
// a Trie's nodes, from a pool of Trie<V>::kNodeSize.

#include <cstdint>
#include <cstring>

#include "prim/alloc.h"

namespace prim {

class Pool final : public Alloc {
 public:
  // size is rounded up to 8 and to at least a pointer; a slab must fit 16-bit
  // offsets, so per_slab is cut to what does.
  Pool(Alloc& a, uint32_t size, uint32_t per_slab);
  ~Pool();  // frees every slab, whatever is still out
  Pool(const Pool&) = delete;
  Pool& operator=(const Pool&) = delete;

  void* get();        // nullptr when out of memory
  bool put(void* p);  // false, and nothing done, if p is not an object of this pool that is out
  uint32_t in_use() const;
  uint32_t slabs() const;
  uint32_t size() const { return size_; }
  uint32_t per_slab() const { return per_; }

  // As an Alloc: an object for any request up to size(), whatever the kind;
  // resize succeeds, in place, within size(); stats are the allocator's
  // beneath it.
  void* alloc(uint32_t n, Mem = Mem::Bulk) override { return n <= size_ ? get() : nullptr; }
  void* resize(void* p, uint32_t n) override {
    if (!p) return alloc(n);
    return n <= size_ && size_of(p) ? p : nullptr;
  }
  void free(void* p) override {
    if (p) put(p);
  }
  uint32_t size_of(const void* p) const override;  // size() for an object that is out, else 0
  Stats stats(Mem kind) const override { return a_->stats(kind); }

 private:
  struct Slab {
    Slab* next;
    uint16_t free;  // the first free object's offset from the slab; 0 when none
    uint16_t used;
  };
  // Where the objects start: after the header and the bits, at a multiple of 8,
  // so they are aligned as the arena aligns the slab.
  static uint32_t offset_for(uint32_t per) { return (uint32_t(sizeof(Slab)) + (per + 7) / 8 + 7) & ~7u; }
  static uint16_t& link(void* slab, uint32_t off) {
    return *reinterpret_cast<uint16_t*>(static_cast<char*>(slab) + off);
  }
  static uint8_t* bits(Slab* s) { return reinterpret_cast<uint8_t*>(s + 1); }
  Slab** owner(const void* p, uint32_t* i) const;  // the link to p's slab, and p's index; nullptr if p is no object here

  Alloc* a_;
  Slab* s_ = nullptr;
  uint16_t size_;
  uint16_t per_;
  uint16_t off_;
};

// ---- Implementation ----

inline Pool::Pool(Alloc& a, uint32_t size, uint32_t per_slab) : a_(&a) {
  // Rounded, then cut to the largest object a slab of one can hold.
  size = ((size < sizeof(void*) ? sizeof(void*) : size) + 7) & ~7u;
  const uint32_t top = (0xFFFFu - offset_for(1)) & ~7u;
  size = size > top ? top : size;
  uint32_t per = per_slab ? per_slab : 1;
  if (per > 0xFFFFu / size) per = 0xFFFFu / size;
  // The bits shrink with the count, so one cut is enough.
  const uint32_t most = (0xFFFFu - offset_for(per)) / size;
  per = per > most ? most : per;
  size_ = uint16_t(size);
  per_ = uint16_t(per);
  off_ = uint16_t(offset_for(per));
}

inline Pool::~Pool() {
  while (Slab* s = s_) {
    s_ = s->next;
    a_->free(s);
  }
}

inline Pool::Slab** Pool::owner(const void* p, uint32_t* i) const {
  for (Slab* const* at = &s_; *at; at = &(*at)->next) {
    const uintptr_t off = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(*at) - off_;
    if (off < uintptr_t(per_) * size_ && off % size_ == 0) {
      *i = uint32_t(off / size_);
      return const_cast<Slab**>(at);
    }
  }
  return nullptr;
}

inline void* Pool::get() {
  Slab* s = s_;
  while (s && !s->free) s = s->next;
  if (!s) {
    s = static_cast<Slab*>(a_->alloc(off_ + per_ * size_));
    if (!s) return nullptr;
    // Every object onto the slab's free list, the first at its head; none out.
    uint32_t next = 0;
    for (uint32_t i = per_; i--;) {
      const uint32_t o = off_ + i * size_;
      link(s, o) = uint16_t(next);
      next = o;
    }
    std::memset(bits(s), 0, (per_ + 7u) / 8);
    s->free = uint16_t(next);
    s->used = 0;
    s->next = s_;
    s_ = s;
  }
  const uint32_t o = s->free;
  const uint32_t i = (o - off_) / size_;
  s->free = link(s, o);
  bits(s)[i / 8] |= uint8_t(1u << (i % 8));
  ++s->used;
  return reinterpret_cast<char*>(s) + o;
}

inline bool Pool::put(void* p) {
  uint32_t i;
  Slab** at = owner(p, &i);
  if (!at) return false;
  Slab* s = *at;
  uint8_t& b = bits(s)[i / 8];
  const uint8_t m = uint8_t(1u << (i % 8));
  if (!(b & m)) return false;  // not out: put back already, or never got
  b = uint8_t(b & ~m);
  const uint32_t o = off_ + i * size_;
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

inline uint32_t Pool::size_of(const void* p) const {
  uint32_t i;
  Slab** at = owner(p, &i);
  return at && (bits(*at)[i / 8] >> (i % 8)) & 1 ? size_ : 0;
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
