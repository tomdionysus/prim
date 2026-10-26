#include <list>

#include "check.h"
#include "prim/list.h"

// The intrusive list: each operation, an object on two lists at once, and a
// seeded run against std::list.

namespace {

struct A;
struct B;
struct Item : prim::ListHook<A>, prim::ListHook<B> {
  int v = 0;
};
using LA = prim::List<Item, A>;
using LB = prim::List<Item, B>;

bool same(const LA& l, const std::list<int>& o) {
  auto it = o.begin();
  for (Item* x = l.front(); x; x = l.next(*x), ++it) {
    if (it == o.end() || x->v != *it) return false;
  }
  if (it != o.end() || l.size() != o.size()) return false;
  // And backwards.
  auto rit = o.rbegin();
  for (Item* x = l.back(); x; x = l.prev(*x), ++rit) {
    if (x->v != *rit) return false;
  }
  return true;
}

}  // namespace

TEST(list_each_operation) {
  Item it[5];
  for (int i = 0; i < 5; ++i) it[i].v = i;
  LA l;
  CHECK(l.empty() && !l.front() && !l.back() && !l.pop_front() && l.size() == 0);
  CHECK(!LA::linked(it[0]));
  l.push_back(it[1]);
  l.push_front(it[0]);
  l.push_back(it[3]);
  l.insert_after(it[1], it[2]);
  CHECK(same(l, {0, 1, 2, 3}));
  CHECK(LA::linked(it[2]));
  l.move_front(it[3]);
  CHECK(same(l, {3, 0, 1, 2}));
  LA::remove(it[0]);
  CHECK(!LA::linked(it[0]));
  CHECK(same(l, {3, 1, 2}));
  CHECK_EQ(l.pop_front(), &it[3]);
  CHECK(same(l, {1, 2}));
  CHECK(!l.next(*l.back()) && !l.prev(*l.front()));
}

TEST(list_an_object_on_two_lists) {
  Item it[3];
  LA la;
  LB lb;
  for (int i = 0; i < 3; ++i) {
    it[i].v = i;
    la.push_back(it[i]);
    lb.push_front(it[i]);
  }
  LA::remove(it[1]);
  CHECK(LB::linked(it[1]) && !LA::linked(it[1]));
  CHECK_EQ(lb.size(), 3u);
  CHECK_EQ(la.size(), 2u);
  CHECK_EQ(lb.front(), &it[2]);
}

TEST(list_random_against_std) {
  Item it[32];
  for (int i = 0; i < 32; ++i) it[i].v = i;
  LA l;
  std::list<int> o;
  check::Rng rng(2);
  bool ok = true;
  for (int op = 0; op < 20000 && ok; ++op) {
    Item& x = it[rng.below(32)];
    const uint32_t r = rng.below(5);
    if (!LA::linked(x)) {
      if (r < 2) {
        l.push_front(x);
        o.push_front(x.v);
      } else {
        l.push_back(x);
        o.push_back(x.v);
      }
    } else if (r < 3) {
      LA::remove(x);
      o.remove(x.v);
    } else {
      l.move_front(x);
      o.remove(x.v);
      o.push_front(x.v);
    }
    ok = same(l, o);
  }
  CHECK(ok);
}
