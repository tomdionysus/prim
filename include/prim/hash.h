#pragma once

// Intrusive chained hash table. The link (one pointer) lives in the object,
// which derives from HashHook<Tag> once for each table it can be in. A link
// not in a table is null, so an object knows whether it is in one, and a
// second insert is refused. The table is its allocator, a bucket array and
// three counts; the buckets double when the table holds twice as many
// objects as buckets, so a chain averages under two. If they cannot, the
// table goes on with longer chains and tries again only once its count has
// doubled: an insert fails for want of memory only when there are no buckets
// at all.
//
// Traits give the key: using Key; static Key key(const T&) (or const Key&);
// static uint32_t hash(const Key&); static bool eq(const Key&, const Key&).
// A key a peer chooses is hashed with prim::hash32 (siphash.h), keyed at
// boot, so a peer cannot choose keys that collide.

#include <cstdint>

#include "prim/alloc.h"
#include "prim/assert.h"

namespace prim {

struct HashLink {
  HashLink* next = nullptr;
};

template <class Tag = void>
struct HashHook : HashLink {};

namespace detail {
// The end of every chain: never read or written, only compared with, so that
// a null link can mean "in no table".
inline constexpr HashLink hash_end{};
}  // namespace detail

// The part that does not depend on the type, written once.
class HashCore {
 protected:
  using Fn = uint32_t (*)(const HashLink*);  // the hash of an object's key

  explicit HashCore(Alloc& a) : a_(&a) {}
  ~HashCore();
  static HashLink* end() { return const_cast<HashLink*>(&detail::hash_end); }
  HashLink** slot(uint32_t h) const { return &b_[h & (nb_ - 1)]; }
  bool put(HashLink* x, uint32_t h, Fn fn);
  bool take(HashLink* x, uint32_t h);

  Alloc* a_;
  HashLink** b_ = nullptr;
  uint32_t nb_ = 0;     // buckets, a power of two
  uint32_t n_ = 0;      // objects
  uint32_t retry_ = 0;  // after a growth that failed, the count at which to try again
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
  static bool linked(const T& x) { return hook(x)->next != nullptr; }  // in a table of this tag

  // False if x is in a table already, or there are no buckets and none can be had.
  bool insert(T& x) {
    const bool unlinked = !linked(x);
    PRIM_ASSERT(unlinked);
    return unlinked && put(hook(x), Tr::hash(Tr::key(x)), fn);
  }
  bool remove(const T& x) { return take(hook(x), Tr::hash(Tr::key(x))); }  // false if x is not in this table

  // Several objects may share a key: find gives the one most recently
  // inserted, find_next the one inserted before it, until nullptr.
  T* find(const Key& k) const {
    if (!nb_) return nullptr;
    for (HashLink* l = *slot(Tr::hash(k)); l != end(); l = l->next) {
      if (Tr::eq(Tr::key(*obj(l)), k)) return obj(l);
    }
    return nullptr;
  }
  T* find_next(const T& x) const {
    if (!linked(x)) return nullptr;
    for (HashLink* l = hook(x)->next; l != end(); l = l->next) {
      if (Tr::eq(Tr::key(*obj(l)), Tr::key(x))) return obj(l);
    }
    return nullptr;
  }

  // Every object, in no order. f may remove the object it is given.
  template <class F>
  void each(F f) const {
    for (uint32_t i = 0; i < nb_; ++i) {
      for (HashLink *l = b_[i], *next; l != end(); l = next) {
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

// Every object still in the table is let go, so it can go into another.
inline HashCore::~HashCore() {
  for (uint32_t i = 0; i < nb_; ++i) {
    for (HashLink *l = b_[i], *next; l != end(); l = next) {
      next = l->next;
      l->next = nullptr;
    }
  }
  a_->free(b_);
}

inline bool HashCore::put(HashLink* x, uint32_t h, Fn fn) {
  if (n_ >= 2 * nb_ && n_ >= retry_) {
    // Two objects a bucket: double, or start at 4. Each object goes to its new
    // bucket by its own hash.
    const uint32_t nb = nb_ ? nb_ * 2 : 4;
    auto** b = static_cast<HashLink**>(a_->alloc(nb * sizeof(HashLink*)));
    if (b) {
      for (uint32_t i = 0; i < nb; ++i) b[i] = end();
      for (uint32_t i = 0; i < nb_; ++i) {
        for (HashLink *l = b_[i], *next; l != end(); l = next) {
          next = l->next;
          HashLink** s = &b[fn(l) & (nb - 1)];
          l->next = *s;
          *s = l;
        }
      }
      a_->free(b_);
      b_ = b;
      nb_ = nb;
      retry_ = 0;
    } else if (!nb_) {
      return false;
    } else {
      retry_ = 2 * n_;  // longer chains for now; try again when the count has doubled
    }
  }
  HashLink** s = slot(h);
  x->next = *s;
  *s = x;
  ++n_;
  return true;
}

inline bool HashCore::take(HashLink* x, uint32_t h) {
  if (!nb_ || !x->next) return false;
  for (HashLink** s = slot(h); *s != end(); s = &(*s)->next) {
    if (*s == x) {
      *s = x->next;
      x->next = nullptr;
      --n_;
      return true;
    }
  }
  return false;
}

}  // namespace prim
