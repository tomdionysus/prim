#pragma once

// Path-compressed binary trie (PATRICIA) for longest-prefix match. A key is
// up to 64 bits, held from the top: a prefix of length n is its top n bits,
// the rest ignored. Each node is a prefix, or a junction where two prefixes
// part; junctions are made and freed as prefixes come and go, so the trie
// holds at most twice as many nodes as prefixes. A lookup tests at most one
// bit a level and compares at most one key a level.

#include <cstdint>
#include <initializer_list>

#include "prim/alloc.h"

namespace prim {

class TrieCore {
 public:
  struct Node {
    Node* c[2];
    uint64_t key;  // the top len bits; the rest zero
    void* val;     // nullptr at a junction
    uint8_t len;
  };

  explicit TrieCore(Alloc& a) : a_(&a) {}
  ~TrieCore();
  TrieCore(const TrieCore&) = delete;
  TrieCore& operator=(const TrieCore&) = delete;

  bool insert(uint64_t key, uint8_t len, void* val);  // false if held already, or out of memory
  void* remove(uint64_t key, uint8_t len);            // the value it held, or nullptr
  void* replace(uint64_t key, uint8_t len, void* val);  // the value it held, now val; nullptr, nothing done, if not held
  void* exact(uint64_t key, uint8_t len) const;
  void* match(uint64_t key) const;                    // the longest prefix holding key
  uint32_t size() const { return n_; }                // prefixes
  uint32_t nodes() const { return nodes_; }           // prefixes and junctions

 private:
  static uint64_t mask(uint8_t len) { return len ? ~uint64_t(0) << (64 - len) : 0; }
  static int bit(uint64_t key, uint8_t i) { return int(key >> (63 - i)) & 1; }
  // How many top bits a and b share, at most lim.
  static uint8_t common(uint64_t a, uint64_t b, uint8_t lim) {
    const uint64_t x = a ^ b;
    const uint8_t c = x ? uint8_t(__builtin_clzll(x)) : 64;
    return c < lim ? c : lim;
  }
  // The slot that holds the node for (key, len), or the empty slot where it would go,
  // with the slot of its parent.
  static Node** find(Node** s, uint64_t key, uint8_t len, Node*** parent) {
    for (Node* n; (n = *s) && n->len < len && common(n->key, key, n->len) == n->len;) {
      *parent = s;
      s = &n->c[bit(key, n->len)];
    }
    return s;
  }

  Node* make(uint64_t key, uint8_t len, void* val);
  void drop(Node* n);
  Node* held(uint64_t key, uint8_t len) const;  // the node holding exactly this prefix, or nullptr
  Alloc* a_;
  Node* root_ = nullptr;
  uint32_t n_ = 0;
  uint32_t nodes_ = 0;
};

// The typed face: values are pointers to V, which the trie does not own.
template <class V>
class Trie : TrieCore {
 public:
  static constexpr uint32_t kNodeSize = sizeof(TrieCore::Node);  // what each node allocates, for a Pool
  explicit Trie(Alloc& a) : TrieCore(a) {}
  bool insert(uint64_t key, uint8_t len, V& v) { return TrieCore::insert(key, len, &v); }
  V* remove(uint64_t key, uint8_t len) { return static_cast<V*>(TrieCore::remove(key, len)); }
  V* replace(uint64_t key, uint8_t len, V& v) { return static_cast<V*>(TrieCore::replace(key, len, &v)); }
  V* exact(uint64_t key, uint8_t len) const { return static_cast<V*>(TrieCore::exact(key, len)); }
  V* match(uint64_t key) const { return static_cast<V*>(TrieCore::match(key)); }
  using TrieCore::nodes;
  using TrieCore::size;
};

// ---- Implementation ----

inline TrieCore::~TrieCore() {
  // Every node, without recursion: along any path the lengths rise from 0 to
  // at most 64, so a stack of one node a level and the root is enough.
  Node* stack[66];
  uint32_t n = 0;
  if (root_) stack[n++] = root_;
  while (n) {
    Node* x = stack[--n];
    if (x->c[0]) stack[n++] = x->c[0];
    if (x->c[1]) stack[n++] = x->c[1];
    a_->free(x);
  }
}

inline TrieCore::Node* TrieCore::make(uint64_t key, uint8_t len, void* val) {
  Node* n = static_cast<Node*>(a_->alloc(sizeof(Node)));
  if (n) {
    *n = Node{{nullptr, nullptr}, key, val, len};
    ++nodes_;
  }
  return n;
}

inline void TrieCore::drop(Node* n) {
  a_->free(n);
  --nodes_;
}

inline bool TrieCore::insert(uint64_t key, uint8_t len, void* val) {
  if (len > 64 || !val) return false;
  key &= mask(len);
  Node** s = &root_;
  for (Node* cur; (cur = *s); s = &cur->c[bit(key, cur->len)]) {
    const uint8_t cp = common(cur->key, key, cur->len < len ? cur->len : len);
    if (cp == cur->len && cp < len) continue;  // cur holds key: go down
    if (cp == cur->len) {                      // cur is (key, len)
      if (cur->val) return false;
      cur->val = val;
      ++n_;
      return true;
    }
    // The new prefix goes above cur: itself, if it holds cur, else a junction where they part.
    Node* n = make(key, len, val);
    Node* top = n && cp != len ? make(key & mask(cp), cp, nullptr) : n;
    if (!top) {
      if (n) drop(n);
      return false;
    }
    top->c[bit(cur->key, cp)] = cur;
    if (top != n) top->c[bit(key, cp)] = n;
    *s = top;
    ++n_;
    return true;
  }
  if (!(*s = make(key, len, val))) return false;
  ++n_;
  return true;
}

inline void* TrieCore::remove(uint64_t key, uint8_t len) {
  if (len > 64) return nullptr;
  key &= mask(len);
  Node** ps = nullptr;
  Node** s = find(&root_, key, len, &ps);
  Node* n = *s;
  if (!n || n->len != len || n->key != key || !n->val) return nullptr;
  void* v = n->val;
  n->val = nullptr;
  --n_;
  // A node with no value and fewer than two children goes; then its parent, if
  // that was a junction and is left with one child.
  for (Node** at : {s, ps}) {
    Node* x = at ? *at : nullptr;
    if (!x || x->val || (x->c[0] && x->c[1])) break;
    *at = x->c[!x->c[0]];
    drop(x);
  }
  return v;
}

inline TrieCore::Node* TrieCore::held(uint64_t key, uint8_t len) const {
  if (len > 64) return nullptr;
  key &= mask(len);
  Node** ps = nullptr;
  Node* n = *find(const_cast<Node**>(&root_), key, len, &ps);
  return n && n->len == len && n->key == key && n->val ? n : nullptr;
}

inline void* TrieCore::exact(uint64_t key, uint8_t len) const {
  const Node* n = held(key, len);
  return n ? n->val : nullptr;
}

inline void* TrieCore::replace(uint64_t key, uint8_t len, void* val) {
  Node* n = val ? held(key, len) : nullptr;
  if (!n) return nullptr;
  void* old = n->val;
  n->val = val;
  return old;
}

inline void* TrieCore::match(uint64_t key) const {
  void* best = nullptr;
  for (const Node* n = root_; n && !((key ^ n->key) & mask(n->len));) {
    if (n->val) best = n->val;
    n = n->len < 64 ? n->c[bit(key, n->len)] : nullptr;
  }
  return best;
}

}  // namespace prim
