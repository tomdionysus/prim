#pragma once

// What every primitive that holds memory allocates from. An Arena is one, a
// Pool another; a test wraps one to fail on demand. Sizes are 32-bit:
// nothing here is bigger.
//
// Memory comes in two kinds. Fast is internal RAM: what every frame or every
// poll touches, and anything an interrupt or a cache-off window reads, goes
// there. Bulk is whatever else there is, PSRAM on a board that has it. A
// request for Bulk is a preference, met from fast memory if there is no
// bulk (above the arena's fast reserve, if it has one), so on a board with
// only internal RAM the kind is ignored. A request for Fast is a
// requirement: it is refused rather than met from bulk, so a hot structure
// never lands in slow memory by the luck of the moment.

#include <cstdint>

namespace prim {

enum class Mem : uint8_t { Bulk, Fast };

// A live reading of one kind of memory. `largest` is the biggest allocation
// that would succeed now; `least` is the least free there has been since the
// memory was added, for a soak.
struct Stats {
  uint32_t free;
  uint32_t used;
  uint32_t largest;
  uint32_t least;
};

struct Alloc {
  virtual void* alloc(uint32_t n, Mem want = Mem::Bulk) = 0;  // nullptr when it cannot
  virtual void* resize(void* p, uint32_t n) = 0;             // nullptr when it cannot, p left as it was
  virtual void free(void* p) = 0;                            // nullptr is ignored
  virtual uint32_t size_of(const void* p) const = 0;         // the block's capacity; 0 if p is not allocated
  virtual Stats stats(Mem kind) const = 0;

 protected:
  ~Alloc() = default;  // never deleted through this
};

// An allocator for one owner, over the arena: it asks for the owner's kind of
// memory (an explicit ask for fast passes through), and keeps what the owner
// holds: bytes, objects, and the most bytes it has held. A container built on
// Account(arena, Mem::Fast) lives in fast memory and is counted to its owner.
class Account final : public Alloc {
 public:
  explicit Account(Alloc& to, Mem kind = Mem::Bulk) : to_(&to), kind_(kind) {}
  void* alloc(uint32_t n, Mem want = Mem::Bulk) override {
    void* p = to_->alloc(n, want == Mem::Fast ? want : kind_);
    if (p) take(to_->size_of(p), 1);
    return p;
  }
  void* resize(void* p, uint32_t n) override {
    if (!p) return alloc(n);  // a first growth is an allocation
    const uint32_t was = to_->size_of(p);
    void* q = to_->resize(p, n);
    if (q) {
      bytes_ -= was;
      take(to_->size_of(q), 0);
    }
    return q;
  }
  void free(void* p) override {
    if (const uint32_t s = to_->size_of(p)) {  // nothing for a null or a block that is not allocated
      bytes_ -= s;
      --objects_;
    }
    to_->free(p);
  }
  uint32_t size_of(const void* p) const override { return to_->size_of(p); }
  Stats stats(Mem kind) const override { return to_->stats(kind); }

  uint32_t bytes() const { return bytes_; }
  uint32_t objects() const { return objects_; }
  uint32_t peak() const { return peak_; }

 private:
  void take(uint32_t s, uint32_t n) {
    bytes_ += s;
    objects_ += n;
    if (bytes_ > peak_) peak_ = bytes_;
  }

  Alloc* to_;
  uint32_t bytes_ = 0;
  uint32_t objects_ = 0;
  uint32_t peak_ = 0;
  Mem kind_;
};

}  // namespace prim
