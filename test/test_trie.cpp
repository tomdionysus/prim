#include <map>
#include <utility>

#include "check.h"
#include "prim/trie.h"

// The trie: each operation, the edge lengths (0 and 64), junctions made and
// freed, allocation that fails at each step, and a seeded run against a
// brute-force longest-prefix match.

namespace {

uint64_t top(uint64_t bits, int len) { return len ? bits << (64 - len) : 0; }  // bits as a prefix of len

struct Route {
  int id;
};

}  // namespace

TEST(trie_each_operation) {
  check::Heap h;
  prim::Trie<Route> t(h.a);
  Route r[6] = {{0}, {1}, {2}, {3}, {4}, {5}};
  CHECK(!t.match(0) && !t.remove(0, 0) && !t.exact(0, 0));
  CHECK(t.insert(top(0b1010, 4), 4, r[1]));
  CHECK(t.insert(top(0b10, 2), 2, r[2]));     // above it
  CHECK(t.insert(top(0b1011, 4), 4, r[3]));   // beside it: a junction at 3 bits
  CHECK(t.insert(top(0b101, 3), 3, r[4]));    // at the junction itself
  CHECK(!t.insert(top(0b101, 3), 3, r[5]));   // held already
  CHECK(t.insert(0, 0, r[0]));                // the default
  CHECK(t.insert(~0ull, 64, r[5]));           // a full-length key
  CHECK_EQ(t.size(), 6u);
  CHECK_EQ(t.match(top(0b1010, 4) | 1), &r[1]);
  CHECK_EQ(t.match(top(0b1011, 4)), &r[3]);
  CHECK_EQ(t.match(top(0b100, 3)), &r[2]);
  CHECK_EQ(t.match(top(0b0, 1)), &r[0]);
  CHECK_EQ(t.match(~0ull), &r[5]);
  CHECK_EQ(t.match(~0ull - 1), &r[0]);
  CHECK_EQ(t.exact(top(0b101, 3) | 0xFF, 3), &r[4]);  // bits below the length are ignored
  CHECK(!t.exact(top(0b100, 3), 3));
  CHECK(!t.exact(top(0b1, 1), 1));  // a junction is not a prefix
  CHECK(!t.insert(0, 65, r[0]) && !t.remove(0, 65) && !t.exact(0, 65));
  CHECK_EQ(t.remove(top(0b101, 3), 3), &r[4]);  // two children: stays as a junction
  CHECK(!t.remove(top(0b101, 3), 3));
  CHECK_EQ(t.match(top(0b1011, 4)), &r[3]);
  CHECK_EQ(t.remove(top(0b1010, 4), 4), &r[1]);  // a leaf: its junction goes too
  CHECK_EQ(t.match(top(0b1010, 4)), &r[2]);
  CHECK(!t.remove(top(0b1111, 4), 4));
}

TEST(trie_junctions_are_freed_with_the_last_prefix) {
  check::Heap h;
  const uint32_t before = h.a.used_bytes();
  {
    prim::Trie<Route> t(h.a);
    Route r{7};
    for (int i = 0; i < 16; ++i) CHECK(t.insert(top(uint64_t(i), 8), 8, r));
    CHECK(t.nodes() > t.size());
    for (int i = 0; i < 16; ++i) CHECK(t.remove(top(uint64_t(i), 8), 8) == &r);
    CHECK_EQ(t.nodes(), 0u);
    CHECK_EQ(h.a.used_bytes(), before);
    for (int i = 0; i < 4; ++i) t.insert(top(uint64_t(i), 4), 4, r);  // left for the destructor
  }
  CHECK_EQ(h.a.used_bytes(), before);
}

TEST(trie_out_of_memory_at_each_step) {
  check::Heap h;
  Route r{1};
  {
    check::Failing f(h.a, 0);  // a node at an empty slot
    prim::Trie<Route> t(f);
    CHECK(!t.insert(top(0b1, 1), 1, r));
    CHECK_EQ(t.nodes(), 0u);
  }
  for (int at : {1, 2}) {
    check::Failing f(h.a, at);  // a prefix beside another: the node fails, then its junction
    prim::Trie<Route> t(f);
    CHECK(t.insert(top(0b0, 1), 1, r));
    CHECK(!t.insert(top(0b11, 2), 2, r));
    CHECK_EQ(t.nodes(), 1u);  // nothing left behind
    CHECK_EQ(h.a.check(), 0);
  }
  {
    check::Failing f(h.a, 1);  // a prefix above another
    prim::Trie<Route> t(f);
    CHECK(t.insert(top(0b10, 2), 2, r));
    CHECK(!t.insert(top(0b1, 1), 1, r));
    CHECK_EQ(t.nodes(), 1u);
  }
  prim::TrieCore c(h.a);
  CHECK(!c.insert(0, 0, nullptr));  // a null value would read as a junction: refused
}

TEST(trie_random_against_brute_force) {
  check::Heap h;
  prim::Trie<Route> t(h.a);
  std::map<std::pair<int, uint64_t>, Route*> o;  // (len, prefix) -> value
  static Route r[64];
  check::Rng rng(5);
  bool ok = true;
  for (int op = 0; op < 20000 && ok; ++op) {
    // Prefixes in a narrow band of the space, so that they nest and part often.
    const int len = int(rng.below(10));
    const uint64_t key = top(rng.below(1u << 9), 9) & (len ? ~0ull << (64 - len) : 0);
    auto it = o.find({len, key});
    if (it == o.end()) {
      Route& v = r[rng.below(64)];
      ok = t.insert(key, uint8_t(len), v);
      o[{len, key}] = &v;
    } else {
      ok = t.remove(key, uint8_t(len)) == it->second;
      o.erase(it);
    }
    const uint64_t probe = top(rng.below(1u << 9), 9) | rng.below(1u << 20);
    Route* want = nullptr;
    int best = -1;
    for (auto& [k, v] : o) {
      const uint64_t m = k.first ? ~0ull << (64 - k.first) : 0;
      if ((probe & m) == k.second && k.first > best) {
        best = k.first;
        want = v;
      }
    }
    ok = ok && t.match(probe) == want && t.size() == o.size() && t.nodes() <= 2 * t.size();
  }
  CHECK(ok);
}

TEST(trie_lookups_that_stop_short) {
  check::Heap h;
  prim::Trie<Route> t(h.a);
  Route r{1};
  CHECK(t.insert(top(0b1, 1), 1, r));
  CHECK(t.insert(top(0b111, 3), 3, r));
  CHECK(!t.exact(top(0b01, 2), 2));    // parts from 0b1 on the way down
  CHECK(!t.remove(top(0b01, 2), 2));
  CHECK(!t.exact(top(0b11, 2), 2));    // stops at 0b111: longer
  CHECK(!t.exact(top(0b110, 3), 3));   // stops at 0b111: the same length, another key
  CHECK(!t.remove(top(0b110, 3), 3));
  CHECK_EQ(t.exact(top(0b111, 3), 3), &r);
}

TEST(trie_replace_swaps_a_held_value_only) {
  check::Heap h;
  prim::Trie<Route> t(h.a);
  Route r1{1}, r2{2};
  CHECK(t.insert(top(0b10, 2), 2, r1));
  CHECK(t.insert(top(0b1011, 4), 4, r1));  // a junction at 3 bits above it
  CHECK(!t.replace(top(0b11, 2), 2, r2));   // not held
  CHECK(!t.replace(top(0b101, 3), 3, r2));  // a junction is not held
  CHECK(!t.replace(0, 65, r2));
  CHECK_EQ(t.replace(top(0b10, 2), 2, r2), &r1);
  CHECK_EQ(t.exact(top(0b10, 2), 2), &r2);
  CHECK_EQ(t.match(top(0b1011, 4)), &r1);  // the other untouched
  CHECK_EQ(t.size(), 2u);
  prim::TrieCore c(h.a);
  CHECK(!c.replace(0, 0, nullptr));  // a null value would read as a junction: refused
}
