#pragma once

// A heap over memory it is given, with boundary tags. A block is one word,
// the head, then its capacity. The head holds the capacity and three flags:
// this block allocated, the block before it allocated, and the block in fast
// memory. A free block keeps, in its capacity, its links on a free list and,
// in its last word, its capacity again (the foot), so the block after it can
// find its head and merge with it. An allocated block costs one word. Each
// area ends in a sentinel head, allocated and empty, so nothing reads past it.
//
// Free blocks are kept by kind of memory (alloc.h) and by size class, so a
// free is O(1) and an allocation looks at one class and then takes the first
// block of the next class that has one. No two free blocks touch. Every
// pointer given to free, resize or size_of is checked against the memory the
// arena was given and against its neighbours before it is believed. Not
// thread safe: the caller locks around every call.

#include <cstdint>
#include <cstring>

#include "prim/alloc.h"
#include "prim/assert.h"

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
  // Size classes of free blocks: class b holds capacities from 2^(b+4) to
  // 2^(b+5) - 1, the first everything smaller and the last everything larger.
  static constexpr int kBins = 10;
  static_assert((kAlign & (kAlign - 1)) == 0 && kAlign >= 8);

  struct Free {
    Word head;
    Free* prev;
    Free* next;
  };

  // The capacity an allocation of n octets gets: n rounded up so the block is
  // a multiple of the alignment, and at least the smallest block; 0 if n is
  // too big. And a head's capacity, and a capacity's class.
  static uint32_t capacity_for(uint32_t n);
  static uint32_t capacity_of(Word head) { return uint32_t((head & ~Word(7)) | kCapLow); }
  static Word pack(uint32_t cap) { return cap & ~Word(7); }  // a capacity as a head holds it, room for the flags
  static int bin_of(uint32_t cap) {
    const int b = 27 - __builtin_clz(cap | 16u);  // log2 of the capacity, less 4, at least 0
    return b < kBins ? b : kBins - 1;
  }

  // Adds an area of one kind of memory, trimmed to the alignment at both
  // ends; one too small for a block and its sentinel is ignored. Areas never
  // merge. A board with only internal RAM adds it all as Fast.
  void add(void* p, uint32_t n, Mem kind = Mem::Bulk);

  // A bulk request that finds no bulk is met from fast memory only while at
  // least this much fast memory would stay free. 0, the default, lets bulk
  // take fast memory to the last octet.
  void reserve_fast(uint32_t n) { reserve_ = n; }
  uint32_t fast_reserve() const { return reserve_; }

  void* alloc(uint32_t n, Mem want = Mem::Bulk) override;
  void* resize(void* p, uint32_t n) override;
  void free(void* p) override;
  uint32_t size_of(const void* p) const override;
  Stats stats(Mem kind) const override;

  // Over both kinds.
  uint32_t free_bytes() const { return free_[0] + free_[1]; }
  uint32_t used_bytes() const { return used_[0] + used_[1]; }
  uint32_t largest_free() const;
  uint32_t blocks() const { return blocks_; }   // allocated and free
  uint32_t bad_frees() const { return bad_; }    // a free or resize of what is not allocated
  uint32_t failures() const { return fails_; }  // allocations refused for want of memory
  const Free* free_list(Mem kind, int bin) const { return avail_[kind == Mem::Fast][bin]; }

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
  static Word* hd(const Free* f) { return reinterpret_cast<Word*>(const_cast<Free*>(f)); }
  static Word* foot(Word* h) { return reinterpret_cast<Word*>(bytes(h) + cap(h)); }  // the last word of the capacity
  static Word* after(Word* h) { return reinterpret_cast<Word*>(bytes(h) + kH + cap(h)); }
  // The block before, by its foot: only when that block is free.
  static Word* before(Word* h) { return reinterpret_cast<Word*>(bytes(h) - kH - uint32_t(h[-1])); }
  static Free* as_free(Word* h) { return reinterpret_cast<Free*>(h); }
  // Whether a head's address lies in the memory of kind k: from the first
  // head of its lowest area to the sentinel of its highest.
  bool in(Word x, int k) const { return x >= lo_[k] && x < hi_[k]; }

  Word* valid(const void* p) const;  // p's head, if p is a block this arena has out; else nullptr
  Free* fit(int k, uint32_t c) const;
  void link(Free* f);
  void unlink(Free* f);
  void set_free(Word* h, uint32_t c);
  void took(int k, uint32_t c);

  Free* avail_[2][kBins] = {};
  Word lo_[2] = {~Word(0), ~Word(0)};
  Word hi_[2] = {0, 0};
  uint32_t free_[2] = {0, 0};
  uint32_t used_[2] = {0, 0};
  uint32_t least_[2] = {0, 0};
  uint32_t blocks_ = 0;
  uint32_t bad_ = 0;
  uint32_t fails_ = 0;
  uint32_t reserve_ = 0;
};

// ---- Implementation ----

inline uint32_t Arena::capacity_for(uint32_t n) {
  if (n > ~0u - kA - kH) return 0;
  const uint32_t stride = (n + kH + kA - 1) & ~(kA - 1);
  return (stride < kMin ? kMin : stride) - kH;
}

// The first block of kind k that holds c: from c's own class, where some are
// smaller, else the first of the next class that has any, where none is.
inline Arena::Free* Arena::fit(int k, uint32_t c) const {
  int b = bin_of(c);
  for (Free* f = avail_[k][b]; f; f = f->next) {
    if (cap(hd(f)) >= c) return f;
  }
  while (++b < kBins) {
    if (avail_[k][b]) return avail_[k][b];
  }
  return nullptr;
}

// f onto the front of its class; its capacity is set already.
inline void Arena::link(Free* f) {
  Free*& top = avail_[kind(hd(f))][bin_of(cap(hd(f)))];
  f->prev = nullptr;
  f->next = top;
  if (top) top->prev = f;
  top = f;
}

// f off its class; its capacity is still the one it was linked with.
inline void Arena::unlink(Free* f) {
  (f->prev ? f->prev->next : avail_[kind(hd(f))][bin_of(cap(hd(f)))]) = f->next;
  if (f->next) f->next->prev = f->prev;
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
  set_free(h, uint32_t(last - kH - first) - kH);
  link(as_free(h));
  ++blocks_;
  const int i = kind(h);
  free_[i] += cap(h);
  least_[i] = free_[i];
  if (first < lo_[i]) lo_[i] = first;
  if (last - kH > hi_[i]) hi_[i] = last - kH;
}

inline Arena::Word* Arena::valid(const void* p) const {
  const Word x = reinterpret_cast<Word>(p) - kH;  // where its head would be
  if ((x + kH) & (kA - 1)) return nullptr;         // not a payload's alignment
  if (!in(x, 0) && !in(x, 1)) return nullptr;      // not in memory the arena was given
  Word* h = reinterpret_cast<Word*>(x);
  const int k = kind(h);
  if (!(*h & kUsed) || !in(x, k)) return nullptr;  // not allocated, or marked the other kind
  // Its capacity must end inside the memory, at a block that knows this one is allocated.
  if (cap(h) > hi_[k] - x - kH || !(*after(h) & kPrev)) return nullptr;
  if (!(*h & kPrev)) {
    // The block before is free: its foot must lead back, inside the memory,
    // to a free block whose capacity it is.
    if (x - lo_[k] < kMin) return nullptr;
    const Word f = h[-1];
    if (f > x - lo_[k] - kH) return nullptr;
    const Word* b = before(h);
    if ((*b & kUsed) || cap(b) != f) return nullptr;
  }
  return h;
}

inline void* Arena::alloc(uint32_t n, Mem want) {
  const uint32_t c0 = capacity_for(n);
  if (!c0) return nullptr;
  int k = want == Mem::Fast;
  Free* f = fit(k, c0);
  // No bulk: fast will do, above the reserve; the reverse never.
  if (!f && !k && free_[1] >= c0 && free_[1] - c0 >= reserve_) f = fit(k = 1, c0);
  if (!f) {
    ++fails_;
    return nullptr;
  }
  Word* h = hd(f);
  uint32_t c = cap(h);
  unlink(f);
  if (c - c0 >= kMin) {
    // The rest stays free, a block of its own after this one.
    Word* r = reinterpret_cast<Word*>(bytes(h) + kH + c0);
    *r = kPrev | (*h & kFast);  // the block before it is about to be allocated
    set_free(r, c - c0 - kH);
    link(as_free(r));
    ++blocks_;
    free_[k] -= kH;
    c = c0;
  }
  *h = pack(c) | kUsed | (*h & kKeep);
  *after(h) |= kPrev;
  took(k, c);
  return bytes(h) + kH;
}

inline void Arena::free(void* p) {
  if (!p) return;
  Word* h = valid(p);
  PRIM_ASSERT(h != nullptr);
  if (!h) {
    ++bad_;
    return;
  }
  const int k = kind(h);
  uint32_t c = cap(h);
  used_[k] -= c;
  free_[k] += c;
  Word* n = after(h);
  if (!(*n & kUsed)) {  // the block after is free: it joins this one
    unlink(as_free(n));
    c += kH + cap(n);
    free_[k] += kH;
    --blocks_;
  }
  if (!(*h & kPrev)) {  // the block before is free: it grows over this one
    Word* b = before(h);
    unlink(as_free(b));
    c += kH + cap(b);
    free_[k] += kH;
    --blocks_;
    h = b;
  }
  set_free(h, c);
  link(as_free(h));
}

inline void* Arena::resize(void* p, uint32_t n) {
  if (!p) return alloc(n);
  Word* h = valid(p);
  PRIM_ASSERT(h != nullptr);
  if (!h) {
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
    // Its capacity, and its head, are this block's now. The low-water mark
    // waits until what is not needed has gone back.
    unlink(as_free(nx));
    --blocks_;
    free_[k] -= cap(nx);
    used_[k] += cap(nx) + kH;
    c += kH + cap(nx);
    *h = pack(c) | kUsed | (*h & kKeep);
    *after(h) |= kPrev;
  }
  if (c - want >= kMin) {
    // The rest goes back as a block of its own: marked allocated for a moment,
    // then freed, which lists it and merges it with a free block after it.
    // Between them the two blocks hold a head less than c did.
    Word* r = reinterpret_cast<Word*>(bytes(h) + kH + want);
    *r = pack(c - want - kH) | kUsed | kPrev | (*h & kFast);
    *h = pack(want) | kUsed | (*h & kKeep);
    used_[k] -= kH;
    ++blocks_;
    free(bytes(r) + kH);
  }
  if (free_[k] < least_[k]) least_[k] = free_[k];
  return p;
}

inline uint32_t Arena::size_of(const void* p) const {
  const Word* h = p ? valid(p) : nullptr;
  return h ? cap(h) : 0;
}

inline Stats Arena::stats(Mem kind) const {
  const int i = kind == Mem::Fast;
  uint32_t m = 0;
  // The largest free block is in the highest class that has any.
  for (int b = kBins - 1; b >= 0 && !m; --b) {
    for (const Free* f = avail_[i][b]; f; f = f->next) m = cap(hd(f)) > m ? cap(hd(f)) : m;
  }
  return Stats{free_[i], used_[i], m, least_[i]};
}

inline uint32_t Arena::largest_free() const {
  const uint32_t a = stats(Mem::Bulk).largest, b = stats(Mem::Fast).largest;
  return a > b ? a : b;
}

inline int Arena::check() const {
  for (int k = 0; k < 2; ++k) {
    uint32_t listed = 0;
    for (int b = 0; b < kBins; ++b) {
      const Free* prev = nullptr;
      for (const Free* f = avail_[k][b]; f; prev = f, f = f->next) {
        const Word at = reinterpret_cast<Word>(f);  // tested before anything in it is read
        if (((at + kH) & (kA - 1)) || !in(at, k)) return 1;  // out of alignment, or outside its memory
        Word* h = hd(f);
        if ((*h & kUsed) || cap(h) + kH < kMin) return 2;  // marked used, or too small for a block
        if constexpr (kA > 8) {                              // at 8, every capacity is a block's size
          if ((cap(h) + kH) & (kA - 1)) return 2;
        }
        if (kind(h) != k) return 3;                                // on the other kind's list
        if (f->prev != prev) return 4;                             // a broken back link
        if (!(*after(h) & kUsed)) return 5;                        // a free block after it, not merged
        if (*foot(h) != cap(h) || (*after(h) & kPrev)) return 6;   // the foot, or the next block's mark, wrong
        if (bin_of(cap(h)) != b) return 8;                         // in the wrong class
        listed += cap(h);
      }
    }
    if (listed != free_[k]) return 7;
  }
  return 0;
}

}  // namespace prim
