#pragma once

// A heap over memory it is given, with boundary tags. A block is one word, the head, then its
// capacity. The head holds the capacity and three flags: this block
// allocated, the block before it allocated, and the block in fast memory. A
// free block keeps, in its capacity, its links on the free list and, in its
// last word, its capacity again (the foot), so the block after it can find
// its head and merge with it. An allocated block costs one word. Each area
// ends in a sentinel head, allocated and empty, so nothing reads past it.
// There is a free list for each kind of memory (alloc.h), each in address
// order with no two free blocks touching; allocation is first fit. Not
// thread safe: the caller locks around every call.

#include <cstdint>
#include <cstring>

#include "prim/alloc.h"

#ifndef PRIM_ARENA_ALIGN
#define PRIM_ARENA_ALIGN 8u  // a power of two of at least 8; a test build varies it
#endif

namespace prim {

class Arena final : public Alloc {
 public:
  using Word = uintptr_t;
  static constexpr uint32_t kAlign = PRIM_ARENA_ALIGN;
  static constexpr uint32_t kHeader = sizeof(Word);
  static constexpr Word kUsed = 1;      // this block is allocated
  static constexpr Word kPrevUsed = 2;  // the block before it is allocated, or there is none
  static constexpr Word kFast = 4;      // this block is in fast memory
  // Every capacity is the same modulo 8, so the low three bits of a head are the flags.
  static constexpr Word kCapLow = (kAlign - kHeader) & 7;
  // The smallest block: a head, two links and a foot, as a multiple of the alignment.
  static constexpr uint32_t kMinStride = (kHeader + 3 * sizeof(Word) + kAlign - 1) & ~(kAlign - 1);
  static_assert((kAlign & (kAlign - 1)) == 0 && kAlign >= 8);

  struct Free {
    Word head;
    Free* prev;
    Free* next;
  };

  // The capacity an allocation of n octets gets: n rounded up so the block is
  // a multiple of the alignment, and at least the smallest block; 0 if n is
  // too big. And a head's capacity.
  static uint32_t capacity_for(uint32_t n);
  static uint32_t capacity_of(Word head) { return uint32_t((head & ~Word(7)) | kCapLow); }
  static Word pack(uint32_t cap) { return cap & ~Word(7); }  // a capacity as a head holds it, room for the flags

  // Adds an area of one kind of memory, trimmed to the alignment at both
  // ends; one too small for a block and its sentinel is ignored. Areas never
  // merge. A board with only internal RAM adds it all as Fast.
  void add(void* p, uint32_t n, Mem kind = Mem::Bulk);

  void* alloc(uint32_t n, Mem want = Mem::Bulk) override;
  void* resize(void* p, uint32_t n) override;
  void free(void* p) override;
  uint32_t size_of(const void* p) const override;
  Stats stats(Mem kind) const override;

  // Over both kinds.
  uint32_t free_bytes() const { return free_[0] + free_[1]; }
  uint32_t used_bytes() const { return used_[0] + used_[1]; }
  uint32_t largest_free() const;
  uint32_t blocks() const { return blocks_; }
  uint32_t bad_frees() const { return bad_; }    // a free or resize of what is not allocated
  uint32_t failures() const { return fails_; }  // allocations refused for want of memory
  const Free* free_list(Mem kind) const { return avail_[kind == Mem::Fast]; }

  // The free lists checked against themselves and the counters: 0 when they
  // are right, else the number of the check that failed.
  int check() const;

 private:
  static constexpr uint32_t kH = kHeader, kA = kAlign, kMin = kMinStride;
  static constexpr Word kPrev = kPrevUsed;
  static constexpr Word kKeep = kPrevUsed | kFast;  // what a block keeps through every change of state

  static char* bytes(const void* p) { return const_cast<char*>(static_cast<const char*>(p)); }
  static uint32_t cap(const Word* h) { return capacity_of(*h); }
  static int kind(const Word* h) { return (*h & kFast) != 0; }
  static Word* head(const void* p) { return reinterpret_cast<Word*>(bytes(p) - kH); }
  static Word* foot(Word* h) { return reinterpret_cast<Word*>(bytes(h) + cap(h)); }  // the last word of the capacity
  static Word* after(Word* h) { return reinterpret_cast<Word*>(bytes(h) + kH + cap(h)); }
  // The block before, by its foot: only when that block is free.
  static Word* before(Word* h) { return reinterpret_cast<Word*>(bytes(h) - kH - uint32_t(h[-1])); }
  static Free* as_free(Word* h) { return reinterpret_cast<Free*>(h); }
  // Address order across areas, which are separate objects to the language.
  static bool below(const void* a, const void* b) {
    return reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b);
  }

  Free* fit(int k, uint32_t c) const;
  void link(Free* f);
  void relink(Free* old, Free* f);
  void unlink(Free* f);
  void set_free(Word* h, uint32_t c);
  void took(int k, uint32_t c);

  Free* avail_[2] = {nullptr, nullptr};
  uint32_t free_[2] = {0, 0};
  uint32_t used_[2] = {0, 0};
  uint32_t least_[2] = {0, 0};
  uint32_t blocks_ = 0;
  uint32_t bad_ = 0;
  uint32_t fails_ = 0;
};

// ---- Implementation ----

inline uint32_t Arena::capacity_for(uint32_t n) {
  if (n > ~0u - kA - kH) return 0;
  const uint32_t stride = (n + kH + kA - 1) & ~(kA - 1);
  return (stride < kMin ? kMin : stride) - kH;
}

// The first free block of kind k that holds c.
inline Arena::Free* Arena::fit(int k, uint32_t c) const {
  Free* f = avail_[k];
  while (f && cap(&f->head) < c) f = f->next;
  return f;
}

inline void Arena::unlink(Free* f) {
  (f->prev ? f->prev->next : avail_[kind(&f->head)]) = f->next;
  if (f->next) f->next->prev = f->prev;
}

// f takes old's place on the list.
inline void Arena::relink(Free* old, Free* f) {
  f->prev = old->prev;
  f->next = old->next;
  (f->prev ? f->prev->next : avail_[kind(&f->head)]) = f;
  if (f->next) f->next->prev = f;
}

// f onto the list of its kind, in address order.
inline void Arena::link(Free* f) {
  Free** at = &avail_[kind(&f->head)];
  Free* prev = nullptr;
  Free* next = *at;
  while (next && below(next, f)) {
    prev = next;
    next = next->next;
  }
  f->prev = prev;
  f->next = next;
  (prev ? prev->next : *at) = f;
  if (next) next->prev = f;
}

// h is free with capacity c: its head, keeping its marks, its foot, and the
// mark on the block after it.
inline void Arena::set_free(Word* h, uint32_t c) {
  *h = pack(c) | (*h & kKeep);
  *foot(h) = c;
  *after(h) &= ~kPrev;
}

// c octets of kind k went from free to used.
inline void Arena::took(int k, uint32_t c) {
  free_[k] -= c;
  used_[k] += c;
  if (free_[k] < least_[k]) least_[k] = free_[k];
}

inline void Arena::add(void* p, uint32_t n, Mem k) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(p);
  const uintptr_t end = start + n;
  if (!p || end < start || start > ~uintptr_t(0) - kA - kH) return;
  // The first head, so that its payload is aligned; the sentinel's head, a word
  // short of the aligned end.
  const uintptr_t first = ((start + kH + kA - 1) & ~uintptr_t(kA - 1)) - kH;
  const uintptr_t last = end & ~uintptr_t(kA - 1);
  if (last < first + kH || last - kH - first < kMin) return;
  Word* h = reinterpret_cast<Word*>(first);
  *reinterpret_cast<Word*>(last - kH) = kUsed;
  *h = kPrev | (k == Mem::Fast ? kFast : 0);
  link(as_free(h));
  set_free(h, uint32_t(last - kH - first) - kH);
  ++blocks_;
  free_[kind(h)] += cap(h);
  least_[kind(h)] = free_[kind(h)];
}

inline void* Arena::alloc(uint32_t n, Mem want) {
  const uint32_t c0 = capacity_for(n);
  if (!c0) return nullptr;
  int k = want == Mem::Fast;
  Free* f = fit(k, c0);
  if (!f && !k) f = fit(k = 1, c0);  // no bulk: fast will do; the reverse never
  if (!f) {
    ++fails_;
    return nullptr;
  }
  Word* h = &f->head;
  uint32_t c = cap(h);
  if (c - c0 >= kMin) {
    // The rest stays free, in f's place on the list.
    Free* r = as_free(reinterpret_cast<Word*>(bytes(h) + kH + c0));
    r->head = kPrev | (*h & kFast);  // the block before it is about to be allocated
    relink(f, r);
    set_free(&r->head, c - c0 - kH);
    ++blocks_;
    free_[k] -= kH;
    c = c0;
  } else {
    unlink(f);
  }
  *h = pack(c) | kUsed | (*h & kKeep);
  *after(h) |= kPrev;
  took(k, c);
  return bytes(h) + kH;
}

inline void Arena::free(void* p) {
  if (!p) return;
  Word* h = head(p);
  if (!(*h & kUsed)) {
    ++bad_;
    return;
  }
  const int k = kind(h);
  uint32_t c = cap(h);
  used_[k] -= c;
  free_[k] += c;
  Word* n = after(h);
  Free* place = nullptr;  // a place on the list that is this block's to take
  if (!(*n & kUsed)) {    // merged with the block after: its place is this block's
    c += kH + cap(n);
    free_[k] += kH;
    --blocks_;
    place = as_free(n);
  }
  if (!(*h & kPrev)) {  // the block before is free: it grows over this one, in its own place
    Word* b = before(h);
    if (place) unlink(place);
    c += kH + cap(b);
    free_[k] += kH;
    --blocks_;
    h = b;
  } else if (place) {
    relink(place, as_free(h));
  } else {
    link(as_free(h));
  }
  set_free(h, c);
}

inline void* Arena::resize(void* p, uint32_t n) {
  if (!p) return alloc(n);
  Word* h = head(p);
  if (!(*h & kUsed)) {
    ++bad_;
    return nullptr;
  }
  const uint32_t want = capacity_for(n);
  if (!want) return nullptr;
  const int k = kind(h);
  uint32_t c = cap(h);
  if (want > c) {
    // In place only over a free block after it that is big enough; else
    // elsewhere, in memory of the same kind.
    Word* nx = after(h);
    if ((*nx & kUsed) || c + kH + cap(nx) < want) {
      void* moved = alloc(n, k ? Mem::Fast : Mem::Bulk);
      if (moved) {
        std::memcpy(moved, p, c);
        free(p);
      }
      return moved;
    }
    unlink(as_free(nx));
    --blocks_;
    took(k, cap(nx));  // its capacity, and its head, are this block's now
    used_[k] += kH;
    c += kH + cap(nx);
    *h = pack(c) | kUsed | (*h & kKeep);
    *after(h) |= kPrev;
  }
  if (c - want >= kMin) {
    // The rest goes back as a block of its own: marked allocated for a moment,
    // then freed, which puts it on the list and merges it with a free block
    // after it. Between them the two blocks hold a head less than c did.
    Word* r = reinterpret_cast<Word*>(bytes(h) + kH + want);
    *r = pack(c - want - kH) | kUsed | kPrev | (*h & kFast);
    *h = pack(want) | kUsed | (*h & kKeep);
    used_[k] -= kH;
    ++blocks_;
    free(bytes(r) + kH);
  }
  return p;
}

inline uint32_t Arena::size_of(const void* p) const {
  return p && (*head(p) & kUsed) ? cap(head(p)) : 0;
}

inline Stats Arena::stats(Mem k) const {
  const int i = k == Mem::Fast;
  uint32_t m = 0;
  for (const Free* f = avail_[i]; f; f = f->next) m = cap(&f->head) > m ? cap(&f->head) : m;
  return Stats{free_[i], used_[i], m, least_[i]};
}

inline uint32_t Arena::largest_free() const {
  const uint32_t a = stats(Mem::Bulk).largest, b = stats(Mem::Fast).largest;
  return a > b ? a : b;
}

inline int Arena::check() const {
  for (int k = 0; k < 2; ++k) {
    uint32_t listed = 0;
    const Free* prev = nullptr;
    for (const Free* f = avail_[k]; f; prev = f, f = f->next) {
      const uintptr_t at = reinterpret_cast<uintptr_t>(f);  // tested before anything in it is read
      if ((at + kH) & (kA - 1)) return 1;                   // out of alignment
      Word* h = reinterpret_cast<Word*>(at);
      if ((*h & kUsed) || cap(h) + kH < kMin) return 2;  // marked used, or too small for a block
      if constexpr (kA > 8) {                                // at 8, every capacity is a block's size
        if ((cap(h) + kH) & (kA - 1)) return 2;
      }
      if (kind(h) != k) return 3;                                                       // on the other kind's list
      if (f->prev != prev) return 4;                                                    // a broken back link
      if (prev && (!below(prev, f) || after(const_cast<Word*>(&prev->head)) == h)) return 5;  // out of order, or not merged
      if (*foot(h) != cap(h) || (*after(h) & kPrev)) return 6;  // the foot, or the next block's mark, wrong
      listed += cap(h);
    }
    if (listed != free_[k]) return 7;
  }
  return 0;
}

}  // namespace prim
