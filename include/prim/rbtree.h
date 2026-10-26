#pragma once

// Intrusive red-black tree. A node is three words: the parent pointer with
// the colour in its low bit, and two children. The object derives from
// RbHook<Tag> once for each tree it can be in. Balancing is written once, on
// nodes, for every tree (rb_insert, rb_erase); a Tree<T> adds only the
// comparisons. Left and right are child 0 and 1, so each mirrored case is
// written once. Equal keys go to the right, so equal objects stay in the
// order they came. A node not in a tree is its own parent.

#include <cstdint>

#include "prim/assert.h"

namespace prim {

struct RbNode {
  uintptr_t pc = reinterpret_cast<uintptr_t>(this);  // parent | black
  RbNode* c[2] = {nullptr, nullptr};
};

struct RbRoot {
  RbNode* root = nullptr;
};

bool rb_linked(const RbNode* n);
void rb_insert(RbRoot& t, RbNode* n, RbNode* parent, int side);  // n becomes parent's child on side
void rb_erase(RbRoot& t, RbNode* n);
RbNode* rb_end(const RbRoot& t, int d);    // the first (d 0) or last (d 1)
RbNode* rb_step(const RbNode* n, int d);   // the next (d 1) or previous (d 0)

template <class Tag = void>
struct RbHook : RbNode {};

// Tr: static int cmp(const T&, const T&); and, for find and lower_bound,
// static int cmp(const K&, const T&) for each key type K: below 0, 0 or above.
template <class T, class Tr, class Tag = void>
class Tree {
 public:
  Tree() = default;
  Tree(const Tree&) = delete;
  Tree& operator=(const Tree&) = delete;

  bool empty() const { return !t_.root; }
  uint32_t size() const {  // a walk: the tree keeps no count
    uint32_t n = 0;
    for (const T* x = first(); x; x = next(*x)) ++n;
    return n;
  }
  T* first() const { return obj(rb_end(t_, 0)); }
  T* last() const { return obj(rb_end(t_, 1)); }
  T* next(const T& x) const { return obj(rb_step(hook(x), 1)); }
  T* prev(const T& x) const { return obj(rb_step(hook(x), 0)); }
  static bool linked(const T& x) { return rb_linked(hook(x)); }
  void remove(T& x) {  // a node in no tree is left alone
    const bool on = linked(x);
    PRIM_ASSERT(on);
    if (on) rb_erase(t_, hook(x));
  }

  void insert(T& x) {  // a node in a tree already is refused
    const bool unlinked = !linked(x);
    PRIM_ASSERT(unlinked);
    if (!unlinked) return;
    RbNode* p = nullptr;
    int side = 0;
    for (RbNode* n = t_.root; n; n = n->c[side]) {
      p = n;
      side = Tr::cmp(x, *obj(n)) >= 0;
    }
    rb_insert(t_, hook(x), p, side);
  }

  // The first object not below k, or nullptr.
  template <class K>
  T* lower_bound(const K& k) const {
    RbNode* best = nullptr;
    for (RbNode* n = t_.root; n;) {
      const bool below = Tr::cmp(k, *obj(n)) > 0;
      if (!below) best = n;
      n = n->c[below];
    }
    return obj(best);
  }

  template <class K>
  T* find(const K& k) const {
    T* x = lower_bound(k);
    return x && Tr::cmp(k, *x) == 0 ? x : nullptr;
  }

 private:
  static RbNode* hook(const T& x) { return &const_cast<RbHook<Tag>&>(static_cast<const RbHook<Tag>&>(x)); }
  static T* obj(RbNode* n) { return n ? &static_cast<T&>(static_cast<RbHook<Tag>&>(*n)) : nullptr; }

  RbRoot t_;
};

// ---- Implementation ----

namespace detail {

constexpr uintptr_t kBlack = 1;

inline RbNode* parent(const RbNode* n) { return reinterpret_cast<RbNode*>(n->pc & ~kBlack); }
inline bool red(const RbNode* n) { return n && !(n->pc & kBlack); }
inline void set_parent(RbNode* n, RbNode* p) { n->pc = reinterpret_cast<uintptr_t>(p) | (n->pc & kBlack); }
inline void paint(RbNode* n, uintptr_t black) { n->pc = (n->pc & ~kBlack) | black; }

// Puts n where old was, under old's parent.
inline void replace(RbRoot& t, RbNode* old, RbNode* n) {
  RbNode* p = parent(old);
  (p ? p->c[p->c[1] == old] : t.root) = n;
}

// Rotates x down to side d: its child on the other side takes its place.
inline void rotate(RbRoot& t, RbNode* x, int d) {
  RbNode* y = x->c[!d];
  x->c[!d] = y->c[d];
  if (y->c[d]) set_parent(y->c[d], x);
  set_parent(y, parent(x));
  replace(t, x, y);
  y->c[d] = x;
  set_parent(x, y);
}

}  // namespace detail

inline bool rb_linked(const RbNode* n) { return n->pc != reinterpret_cast<uintptr_t>(n); }

inline void rb_insert(RbRoot& t, RbNode* n, RbNode* p, int side) {
  n->pc = reinterpret_cast<uintptr_t>(p);  // red
  n->c[0] = n->c[1] = nullptr;
  (p ? p->c[side] : t.root) = n;
  while ((p = detail::parent(n)) && detail::red(p)) {
    RbNode* g = detail::parent(p);  // p is red, so not the root
    const int d = g->c[1] == p;
    RbNode* u = g->c[!d];
    if (detail::red(u)) {
      detail::paint(p, detail::kBlack);
      detail::paint(u, detail::kBlack);
      detail::paint(g, 0);
      n = g;
      continue;
    }
    if (n == p->c[!d]) {
      detail::rotate(t, p, d);
      p = n;
    }
    detail::paint(p, detail::kBlack);
    detail::paint(g, 0);
    detail::rotate(t, g, !d);
    break;  // the subtree's top is black again: nothing above it changed
  }
  detail::paint(t.root, detail::kBlack);
}

inline void rb_erase(RbRoot& t, RbNode* z) {
  RbNode* x;  // what takes the removed node's place, perhaps null
  RbNode* p;  // x's parent
  uintptr_t black;
  if (!z->c[0] || !z->c[1]) {
    x = z->c[!z->c[0]];
    p = detail::parent(z);
    black = z->pc & detail::kBlack;
    detail::replace(t, z, x);
  } else {
    // The successor, which has no left child, takes z's place and colour.
    RbNode* y = z->c[1];
    while (y->c[0]) y = y->c[0];
    x = y->c[1];
    p = detail::parent(y);
    black = y->pc & detail::kBlack;
    detail::replace(t, y, x);
    if (p == z) p = y;
    y->pc = z->pc;
    y->c[0] = z->c[0];
    y->c[1] = z->c[1];
    detail::replace(t, z, y);
    detail::set_parent(y->c[0], y);
    if (y->c[1]) detail::set_parent(y->c[1], y);
  }
  if (x) detail::set_parent(x, p);
  z->pc = reinterpret_cast<uintptr_t>(z);
  if (!black) return;
  // A black node is gone from x's side: make it up.
  while (x != t.root && !detail::red(x)) {
    const int d = p->c[1] == x;
    RbNode* w = p->c[!d];  // not null: the black height of x's side was at least one
    if (detail::red(w)) {
      detail::paint(w, detail::kBlack);
      detail::paint(p, 0);
      detail::rotate(t, p, d);
      w = p->c[!d];
    }
    if (!detail::red(w->c[0]) && !detail::red(w->c[1])) {
      detail::paint(w, 0);
      x = p;
      p = detail::parent(x);
      continue;
    }
    if (!detail::red(w->c[!d])) {
      detail::paint(w->c[d], detail::kBlack);
      detail::paint(w, 0);
      detail::rotate(t, w, !d);
      w = p->c[!d];
    }
    detail::paint(w, p->pc & detail::kBlack);
    detail::paint(p, detail::kBlack);
    detail::paint(w->c[!d], detail::kBlack);
    detail::rotate(t, p, d);
    x = t.root;
  }
  if (x) detail::paint(x, detail::kBlack);
}

inline RbNode* rb_end(const RbRoot& t, int d) {
  RbNode* n = t.root;
  while (n && n->c[d]) n = n->c[d];
  return n;
}

inline RbNode* rb_step(const RbNode* n, int d) {
  if (n->c[d]) {
    n = n->c[d];
    while (n->c[!d]) n = n->c[!d];
    return const_cast<RbNode*>(n);
  }
  RbNode* p;
  while ((p = detail::parent(n)) && n == p->c[d]) n = p;
  return p;
}

}  // namespace prim
