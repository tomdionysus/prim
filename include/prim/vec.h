#pragma once

// Growable array of trivially copyable values, with a maximum count of at
// most 65,535. It starts empty and holds no memory; it grows 4, 8, 16 and so
// on, up to the maximum. The work is done once, on bytes (VecCore), for
// every element type. For small sets, where a scan is fastest, and for
// sorted arrays searched by lower_bound.

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "prim/alloc.h"
#include "prim/sort.h"

namespace prim {

class VecCore {
 protected:
  VecCore(Alloc& a, uint16_t max) : a_(&a), max_(max) {}
  ~VecCore() { a_->free(d_); }
  void* open(uint32_t i, uint32_t esz);              // room at i; nullptr when full or out of memory
  void close(uint32_t i, uint32_t esz, bool swap);  // removes i: shifting what follows, or moving the last in

  Alloc* a_;
  char* d_ = nullptr;
  uint16_t n_ = 0;
  uint16_t cap_ = 0;
  uint16_t max_;
};

template <class T>
class Vec : VecCore {
  static_assert(std::is_trivially_copyable_v<T>);

 public:
  Vec(Alloc& a, uint16_t max) : VecCore(a, max) {}
  Vec(const Vec&) = delete;
  Vec& operator=(const Vec&) = delete;

  uint32_t size() const { return n_; }
  uint32_t capacity() const { return cap_; }
  bool empty() const { return !n_; }
  T* begin() const { return reinterpret_cast<T*>(d_); }
  T* end() const { return begin() + n_; }
  T& operator[](uint32_t i) const { return begin()[i]; }

  bool push(const T& x) { return insert(n_, x); }
  bool insert(uint32_t i, const T& x) {
    void* p = open(i, sizeof(T));
    if (p) std::memcpy(p, &x, sizeof(T));
    return p != nullptr;
  }
  void erase(uint32_t i) { close(i, sizeof(T), false); }
  void erase_swap(uint32_t i) { close(i, sizeof(T), true); }
  void clear() { n_ = 0; }
  template <class Less>
  void sort(Less less) {
    prim::sort(begin(), end(), less);
  }

  // The first index whose element is not less than k, by less(element, k).
  template <class K, class Less>
  uint32_t lower_bound(const K& k, Less less) const {
    uint32_t lo = 0, hi = n_;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (less((*this)[mid], k)) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }
};

// ---- Implementation ----

inline void* VecCore::open(uint32_t i, uint32_t esz) {
  if (n_ == cap_) {
    uint32_t cap = cap_ ? 2u * cap_ : 4u;
    if (cap > max_) cap = max_;
    char* d = cap > cap_ ? static_cast<char*>(a_->resize(d_, cap * esz)) : nullptr;
    if (!d) return nullptr;
    d_ = d;
    cap_ = uint16_t(cap);
  }
  char* at = d_ + i * esz;
  std::memmove(at + esz, at, (n_ - i) * esz);
  ++n_;
  return at;
}

inline void VecCore::close(uint32_t i, uint32_t esz, bool swap) {
  char* at = d_ + i * esz;
  --n_;
  if (!swap) {
    std::memmove(at, at + esz, (n_ - i) * esz);
  } else if (i != n_) {
    std::memcpy(at, d_ + n_ * esz, esz);
  }
}

}  // namespace prim
