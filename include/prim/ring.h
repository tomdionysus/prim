#pragma once

// Growable ring buffer of trivially copyable values, oldest to newest, with
// a maximum count of at most 32,767. When it is full it either refuses the
// newcomer or overwrites the oldest, as it was made to. It grows as the
// array does, up to the maximum; the work is done once, on bytes (RingCore).

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "prim/alloc.h"
#include "prim/assert.h"

namespace prim {

class RingCore {
 protected:
  RingCore(Alloc& a, uint16_t max, bool overwrite) : a_(&a), max_(max & 0x7FFF), over_(overwrite) {}
  ~RingCore() { a_->free(d_); }
  void* push(uint32_t esz);  // the slot for a newest; nullptr when refused
  void* at(uint32_t i, uint32_t esz) const {
    const uint32_t j = head_ + i;
    return d_ + (j >= cap_ ? j - cap_ : j) * esz;
  }
  void pop() {
    head_ = uint16_t(head_ + 1 == cap_ ? 0 : head_ + 1);
    --n_;
  }

  Alloc* a_;
  char* d_ = nullptr;
  uint16_t head_ = 0;
  uint16_t n_ = 0;
  uint16_t cap_ = 0;
  uint16_t max_ : 15;
  uint16_t over_ : 1;
};

template <class T>
class Ring : RingCore {
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  Ring(Alloc& a, uint16_t max, bool overwrite) : RingCore(a, max, overwrite) {}
  Ring(const Ring&) = delete;
  Ring& operator=(const Ring&) = delete;

  uint32_t size() const { return n_; }
  uint32_t capacity() const { return cap_; }
  bool empty() const { return !n_; }
  T& operator[](uint32_t i) const {  // 0 is the oldest; unchecked but for PRIM_ASSERT
    PRIM_ASSERT(i < n_);
    return *static_cast<T*>(at(i, sizeof(T)));
  }
  T* oldest() const { return n_ ? static_cast<T*>(at(0, sizeof(T))) : nullptr; }
  T* newest() const { return n_ ? static_cast<T*>(at(n_ - 1u, sizeof(T))) : nullptr; }

  bool push(const T& x) {
    void* p = RingCore::push(sizeof(T));
    if (p) std::memcpy(p, &x, sizeof(T));
    return p != nullptr;
  }
  void pop() {
    if (n_) RingCore::pop();
  }
  void clear() { head_ = n_ = 0; }
};

// ---- Implementation ----

inline void* RingCore::push(uint32_t esz) {
  if (n_ == cap_) {
    // Grow, oldest first in the new buffer; else, if overwriting, lose the oldest.
    uint32_t cap = cap_ ? 2u * cap_ : 4u;
    if (cap > max_) cap = max_;
    char* d = cap > cap_ ? static_cast<char*>(a_->alloc(cap * esz)) : nullptr;
    if (d) {
      if (d_) {
        const uint32_t first = cap_ - head_;  // from the oldest to the end of the buffer
        std::memcpy(d, d_ + head_ * esz, first * esz);
        std::memcpy(d + first * esz, d_, head_ * esz);
        a_->free(d_);
      }
      d_ = d;
      head_ = 0;
      cap_ = uint16_t(cap);
    } else if (over_ && n_) {
      pop();
    } else {
      return nullptr;
    }
  }
  ++n_;
  return at(n_ - 1u, esz);
}

}  // namespace prim
