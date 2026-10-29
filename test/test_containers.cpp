#include <algorithm>
#include <utility>
#include <deque>
#include <vector>

#include "check.h"
#include "prim/pool.h"
#include "prim/ring.h"
#include "prim/vec.h"

// The growable array, the ring buffer and the pool: each operation, growth
// to the maximum, growth that fails, and seeded runs against std:: oracles.

TEST(vec_each_operation) {
  check::Heap h;
  prim::Vec<int> v(h.a, 6);
  CHECK(v.empty() && v.capacity() == 0 && v.begin() == v.end());
  for (int i = 0; i < 6; ++i) CHECK(v.push(i * 10));  // grows 4, then 6, the maximum
  CHECK_EQ(v.capacity(), 6u);
  CHECK(!v.push(99));  // full
  v.erase(1);          // 0 20 30 40 50
  CHECK(v.insert(1, 15));
  CHECK_EQ(v[1], 15);
  CHECK_EQ(v[2], 20);
  v.erase_swap(0);  // the last moves in: 50 15 20 30 40
  CHECK_EQ(v[0], 50);
  v.erase_swap(4);  // the last itself
  CHECK_EQ(v.size(), 4u);
  CHECK_EQ(v.lower_bound(25, [](int a, int k) { return a < k; }), 3u);  // over 50 15 20 30: not sorted, still a bisection
  v.clear();
  CHECK(v.empty());
  prim::Vec<int> none(h.a, 0);
  CHECK(!none.push(1));
  prim::Ring<int> no_ring(h.a, 0, true);
  CHECK(!no_ring.push(1));
}

TEST(vec_failing_growth_and_sorted_use) {
  check::Heap h;
  check::Failing f(h.a, 1);  // the second growth fails
  prim::Vec<uint32_t> v(f, 100);
  for (uint32_t i = 0; i < 4; ++i) CHECK(v.push(i));
  CHECK(!v.push(4));
  CHECK_EQ(v.size(), 4u);
  // A sorted array kept by lower_bound, against std::vector.
  prim::Vec<uint32_t> s(h.a, 1000);
  std::vector<uint32_t> o;
  check::Rng rng(6);
  bool ok = true;
  auto less = [](uint32_t a, uint32_t k) { return a < k; };
  for (int op = 0; op < 5000 && ok; ++op) {
    const uint32_t k = rng.below(400);
    const uint32_t i = s.lower_bound(k, less);
    if (i < s.size() && s[i] == k) {
      s.erase(i);
      o.erase(o.begin() + i);
    } else {
      ok = s.insert(i, k);
      o.insert(o.begin() + i, k);
    }
    ok = ok && s.size() == o.size();
    for (uint32_t j = 0; ok && j < s.size(); ++j) ok = s[j] == o[j];
  }
  CHECK(ok);
}

TEST(ring_each_operation) {
  check::Heap h;
  prim::Ring<int> keep(h.a, 5, false);
  CHECK(keep.empty() && !keep.oldest() && !keep.newest());
  keep.pop();  // empty: nothing
  for (int i = 0; i < 5; ++i) CHECK(keep.push(i));
  CHECK(!keep.push(5));  // full, refusing
  CHECK_EQ(*keep.oldest(), 0);
  CHECK_EQ(*keep.newest(), 4);
  keep.pop();
  keep.pop();
  CHECK(keep.push(5) && keep.push(6));  // wrapped
  CHECK_EQ(keep[0], 2);
  CHECK_EQ(keep[4], 6);
  keep.clear();
  CHECK(keep.empty() && keep.capacity() == 5u);

  prim::Ring<int> over(h.a, 3, true);
  for (int i = 0; i < 7; ++i) CHECK(over.push(i));
  CHECK_EQ(over.size(), 3u);
  CHECK_EQ(over[0], 4);
  CHECK_EQ(over[2], 6);
}

TEST(ring_growth_that_fails) {
  check::Heap h;
  {
    check::Failing f(h.a, 0);  // no memory at all
    prim::Ring<int> r(f, 10, true);
    CHECK(!r.push(1));
  }
  check::Failing f(h.a, 1, true);  // the first growth works, none after it
  prim::Ring<int> over(f, 10, true);
  for (int i = 0; i < 6; ++i) CHECK(over.push(i));
  CHECK_EQ(over.capacity(), 4u);  // stayed at 4, overwriting
  CHECK_EQ(over[0], 2);
  check::Failing g(h.a, 1);
  prim::Ring<int> keep(g, 10, false);
  for (int i = 0; i < 4; ++i) CHECK(keep.push(i));
  CHECK(!keep.push(4));
}

TEST(ring_random_against_std) {
  check::Heap h;
  prim::Ring<uint32_t> r(h.a, 37, true);
  std::deque<uint32_t> o;
  check::Rng rng(7);
  bool ok = true;
  for (int op = 0; op < 20000 && ok; ++op) {
    if (rng.below(3)) {
      const uint32_t v = rng();
      ok = r.push(v);
      o.push_back(v);
      if (o.size() > 37) o.pop_front();
    } else {
      r.pop();
      if (!o.empty()) o.pop_front();
    }
    ok = ok && r.size() == o.size();
    for (uint32_t i = 0; ok && i < r.size(); ++i) ok = r[i] == o[i];
  }
  CHECK(ok);
}

TEST(pool_each_operation) {
  check::Heap h;
  const uint32_t before = h.a.used_bytes();
  {
    prim::Pool p(h.a, 3, 4);  // rounded up to a pointer, then to 8
    CHECK_EQ(p.size(), 8u);
    CHECK_EQ(p.slabs(), 0u);
    void* o[9];
    for (auto& x : o) CHECK((x = p.get()) != nullptr);
    CHECK_EQ(p.slabs(), 3u);
    CHECK_EQ(p.in_use(), 9u);
    int stranger = 0;
    CHECK(!p.put(&stranger));                         // not ours
    CHECK(!p.put(static_cast<char*>(o[0]) + 1));      // inside an object, not at one
    for (int i = 0; i < 4; ++i) CHECK(p.put(o[i]));   // the first slab made, now at the back, empties and goes
    CHECK_EQ(p.slabs(), 2u);
    for (int i = 4; i < 9; ++i) CHECK(p.put(o[i]));
    CHECK_EQ(p.slabs(), 1u);  // the last stays
    CHECK_EQ(p.in_use(), 0u);
    CHECK(p.get() != nullptr);  // from the slab kept
    CHECK_EQ(p.slabs(), 1u);
    prim::Pool one(h.a, 64, 0);  // a slab of no objects is a slab of one
    CHECK(one.get() && one.get());
    CHECK_EQ(one.slabs(), 2u);
  }
  CHECK_EQ(h.a.used_bytes(), before);  // the destructors freed it all
  check::Failing f(h.a, 0);
  prim::Pool p(f, 16, 4);
  CHECK(!p.get());
}

TEST(pool_random_against_std) {
  // Sizes with every power of two the index arithmetic shifts by, and odd
  // parts from 1 to 125, whose inverses it multiplies by.
  for (uint32_t size : {8u, 24u, 40u, 56u, 256u, 1000u}) {
    check::Heap h;
    prim::Pool p(h.a, size, 8);
    CHECK_EQ(p.size(), size);
    std::vector<unsigned char*> live;
    check::Rng rng(8 + size);
    bool ok = true;
    for (int op = 0; op < 20000 && ok; ++op) {
      // At most 24 KB out, so the run fits the S3 app's smaller test heap.
      if (live.empty() || (live.size() * size < 24 * 1024 && rng.below(2))) {
        auto* o = static_cast<unsigned char*>(p.get());
        ok = o != nullptr;
        for (uint32_t i = 0; ok && i < size; ++i) o[i] = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(o) + i);
        live.push_back(o);
      } else {
        const uint32_t i = rng.below(uint32_t(live.size()));
        unsigned char* o = live[i];
        for (uint32_t j = 0; j < size; ++j) ok = ok && o[j] == static_cast<unsigned char>(reinterpret_cast<uintptr_t>(o) + j);
        // Inside the object, which no other slab can reach: never an object.
        const uint32_t in = 1 + rng.below(size - 1);
        ok = ok && !p.put(o + in) && p.size_of(o + in) == 0 && p.size_of(o) == size;
        ok = ok && p.put(o);
        live[i] = live.back();
        live.pop_back();
      }
      ok = ok && p.in_use() == live.size() && h.a.check() == 0;
    }
    CHECK(ok);
  }
}

TEST(pool_sizes_are_cut_to_what_offsets_can_reach) {
  check::Heap h;
  {
    prim::Pool big(h.a, 1024, 1000);  // more objects a slab than 16-bit offsets can address
    CHECK_EQ(big.per_slab(), 63u);
    void* o[63];
    for (auto& x : o) CHECK((x = big.get()) != nullptr);
    CHECK_EQ(big.slabs(), 1u);  // all from one slab, as near 64 KB as they come
    for (auto& x : o) CHECK(big.put(x));
  }
  prim::Pool huge(h.a, 100000, 1);  // an object too big for an offset: cut to the largest
  CHECK(huge.size() % 8 == 0 && huge.size() < 0x10000 && huge.size() >= 0xFFE0);
  CHECK_EQ(huge.per_slab(), 1u);
  void* p = huge.get();
  CHECK(p && huge.put(p));
}

TEST(pool_a_slab_given_back_to_comes_first) {
  check::Heap h;
  prim::Pool p(h.a, 16, 2);
  void* a = p.get();
  void* b = p.get();  // the first slab, full
  void* c = p.get();  // a second, in front of it
  CHECK(p.slabs() == 2 && a && b && c);
  CHECK(p.put(a));            // the first slab has room again: to the front
  CHECK(p.get() == a);        // and the next get finds it there, not by making a third
  CHECK_EQ(p.slabs(), 2u);
  for (void* x : {a, b, c}) CHECK(p.put(x));
  CHECK_EQ(p.slabs(), 1u);
}

TEST(sort_is_stable_and_matches_std) {
  int none[1];
  prim::sort(none, none, [](int a, int b) { return a < b; });  // nothing
  int one[1] = {5};
  prim::sort(one, one + 1, [](int a, int b) { return a < b; });
  CHECK_EQ(one[0], 5);
  check::Rng rng(11);
  bool ok = true;
  for (int round = 0; round < 200 && ok; ++round) {
    std::vector<std::pair<int, int>> v(rng.below(64));
    for (uint32_t i = 0; i < v.size(); ++i) v[i] = {int(rng.below(8)), int(i)};  // few keys: many equal
    std::vector<std::pair<int, int>> o = v;
    prim::sort(v.data(), v.data() + v.size(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
    std::stable_sort(o.begin(), o.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
    ok = v == o;
  }
  CHECK(ok);
  check::Heap h;
  prim::Vec<int> w(h.a, 10);
  for (int x : {3, 1, 2}) w.push(x);
  w.sort([](int a, int b) { return a < b; });
  CHECK(w[0] == 1 && w[1] == 2 && w[2] == 3);
}
