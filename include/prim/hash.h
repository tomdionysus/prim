#pragma once

// Intrusive chained hash table. The link (one pointer) lives in the object,
// which derives from HashHook<Tag> once for each table it can be in. The
// table is its allocator, a bucket array and two counts; the buckets double
// when the table holds twice as many objects as buckets, so a chain averages
// under two. If they cannot, the table goes on with longer chains: an insert
// fails only when there are no buckets at all.
//
// Traits give the key: using Key; static Key key(const T&) (or const Key&);
// static uint32_t hash(const Key&); static bool eq(const Key&, const Key&).
// A key a peer chooses is hashed with prim::hash32 (siphash.h), keyed at
// boot, so a peer cannot choose keys that collide.

#include <cstdint>

#include "prim/alloc.h"

namespace prim {

struct HashLink {
  HashLink* next = nullptr;
};

template <class Tag = void>
struct HashHook : HashLink {};

// The part that does not depend on the type, written once.
class HashCore {
 protected:
  using Fn = uint32_t (*)(const HashLink*);  // the hash of an object's key

  explicit HashCore(Alloc& a) : a_(&a) {}
  ~HashCore() { a_->free(b_); }
  HashLink** slot(uint32_t h) const { return &b_[h & (nb_ - 1)]; }
  bool put(HashLink* x, uint32_t h, Fn fn);
  bool take(const HashLink* x, uint32_t h);

  Alloc* a_;
  HashLink** b_ = nullptr;
  uint32_t nb_ = 0;  // buckets, a power of two
  uint32_t n_ = 0;   // objects
};

template <class T, class Tr, class Tag = void>
class Hash : HashCore {
 public:
  using Key = typename Tr::Key;
  explicit Hash(Alloc& a) : HashCore(a) {}
  Hash(const Hash&) = delete;
  Hash& operator=(const Hash&) = delete;

  uint32_t size() const { return n_; }
  uint32_t buckets() const { return nb_; }
  bool insert(T& x) { return put(hook(x), Tr::hash(Tr::key(x)), fn); }
  bool remove(const T& x) { return take(hook(x), Tr::hash(Tr::key(x))); }

  // Several objects may share a key: find gives the one most recently
  // inserted, find_next the one inserted before it, until nullptr.
  T* find(const Key& k) const {
    if (!nb_) return nullptr;
    for (HashLink* l = *slot(Tr::hash(k)); l; l = l->next) {
      if (Tr::eq(Tr::key(*obj(l)), k)) return obj(l);
    }
    return nullptr;
  }
  T* find_next(const T& x) const {
    for (HashLink* l = hook(x)->next; l; l = l->next) {
      if (Tr::eq(Tr::key(*obj(l)), Tr::key(x))) return obj(l);
    }
    return nullptr;
  }

  // Every object, in no order. f may remove the object it is given.
  template <class F>
  void each(F f) const {
    for (uint32_t i = 0; i < nb_; ++i) {
      for (HashLink *l = b_[i], *next; l; l = next) {
        next = l->next;
        f(*obj(l));
      }
    }
  }

 private:
  // Reference casts: neither side can be null, so no null test is compiled in.
  static HashLink* hook(const T& x) { return &const_cast<HashHook<Tag>&>(static_cast<const HashHook<Tag>&>(x)); }
  static T* obj(HashLink* l) { return &static_cast<T&>(static_cast<HashHook<Tag>&>(*l)); }
  static uint32_t fn(const HashLink* l) { return Tr::hash(Tr::key(*obj(const_cast<HashLink*>(l)))); }
};

// ---- Implementation ----

inline bool HashCore::put(HashLink* x, uint32_t h, Fn fn) {
  if (n_ >= 2 * nb_) {
    // Two objects a bucket: double, or start at 4. Each object goes to its new
    // bucket by its own hash.
    const uint32_t nb = nb_ ? nb_ * 2 : 4;
    auto** b = static_cast<HashLink**>(a_->alloc(nb * sizeof(HashLink*)));
    if (b) {
      for (uint32_t i = 0; i < nb; ++i) b[i] = nullptr;
      for (uint32_t i = 0; i < nb_; ++i) {
        for (HashLink *l = b_[i], *next; l; l = next) {
          next = l->next;
          HashLink** s = &b[fn(l) & (nb - 1)];
          l->next = *s;
          *s = l;
        }
      }
      a_->free(b_);
      b_ = b;
      nb_ = nb;
    } else if (!nb_) {
      return false;
    }
  }
  HashLink** s = slot(h);
  x->next = *s;
  *s = x;
  ++n_;
  return true;
}

inline bool HashCore::take(const HashLink* x, uint32_t h) {
  if (!nb_) return false;
  for (HashLink** s = slot(h); *s; s = &(*s)->next) {
    if (*s == x) {
      *s = x->next;
      --n_;
      return true;
    }
  }
  return false;
}

}  // namespace prim
