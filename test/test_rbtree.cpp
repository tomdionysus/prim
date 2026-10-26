#include <set>

#include "check.h"
#include "prim/rbtree.h"
#include "prim/timer.h"

// The red-black tree, checked after every change: parent links, no red node
// with a red child, the same black height on every path, keys in order. A
// seeded run against std::multiset; and the timer queue built on it.

namespace {

struct Item : prim::RbHook<> {
  int k = 0;
  int seq = 0;  // the order it went in, for equal keys
};
struct Tr {
  static int cmp(const Item& a, const Item& b) { return a.k - b.k; }
  static int cmp(int k, const Item& b) { return k - b.k; }
};
using T = prim::Tree<Item, Tr>;

bool black(const prim::RbNode* n) { return !n || (n->pc & 1); }
const prim::RbNode* up(const prim::RbNode* n) { return reinterpret_cast<const prim::RbNode*>(n->pc & ~uintptr_t(1)); }

// The black height below n, or -1 if anything is wrong.
int valid(const prim::RbNode* n, const prim::RbNode* parent) {
  if (!n) return 1;
  if (up(n) != parent) return -1;
  if (!black(n) && (!black(n->c[0]) || !black(n->c[1]))) return -1;
  const int l = valid(n->c[0], n), r = valid(n->c[1], n);
  if (l < 0 || l != r) return -1;
  return l + black(n);
}

bool valid(const T& t, const std::multiset<std::pair<int, int>>& o) {
  const Item* first = t.first();
  if (!first) return o.empty();
  const prim::RbNode* root = first;
  while (up(root)) root = up(root);
  if (!black(root) || valid(root, nullptr) < 0) return false;
  // In order both ways, equal keys in the order they came.
  auto it = o.begin();
  for (const Item* x = first; x; x = t.next(*x), ++it) {
    if (it == o.end() || x->k != it->first || x->seq != it->second) return false;
  }
  auto rit = o.rbegin();
  for (const Item* x = t.last(); x; x = t.prev(*x), ++rit) {
    if (x->k != rit->first || x->seq != rit->second) return false;
  }
  return it == o.end();
}

}  // namespace

TEST(rbtree_each_operation) {
  T t;
  Item it[5];
  CHECK(t.empty() && !t.first() && !t.last() && !t.find(1) && !t.lower_bound(1));
  for (int i = 0; i < 5; ++i) {
    it[i].k = (i * 3) % 5 * 10;  // 0 30 10 40 20
    CHECK(!T::linked(it[i]));
    t.insert(it[i]);
    CHECK(T::linked(it[i]));
  }
  CHECK_EQ(t.first()->k, 0);
  CHECK_EQ(t.last()->k, 40);
  CHECK_EQ(t.find(30), &it[1]);
  CHECK(!t.find(25));
  CHECK_EQ(t.lower_bound(25)->k, 30);
  CHECK_EQ(t.lower_bound(-5)->k, 0);
  CHECK(!t.lower_bound(41));
  t.remove(it[1]);
  CHECK(!T::linked(it[1]) && !t.find(30));
  CHECK_EQ(t.next(it[4])->k, 40);  // 20 -> 40, 30 gone
}

TEST(rbtree_random_against_std) {
  for (int round = 0; round < 3; ++round) {
    T t;
    std::multiset<std::pair<int, int>> o;
    static Item it[300];
    check::Rng rng(4 + round);
    const int keys = round == 0 ? 20 : round == 1 ? 200 : 100000;  // many equal keys, some, almost none
    int seq = 0;
    bool ok = true;
    for (int op = 0; op < 6000 && ok; ++op) {
      Item& x = it[rng.below(300)];
      if (T::linked(x)) {
        t.remove(x);
        o.erase(o.find({x.k, x.seq}));
      } else {
        x.k = int(rng.below(keys));
        x.seq = seq++;
        t.insert(x);
        o.insert({x.k, x.seq});
      }
      ok = valid(t, o);
      const int probe = int(rng.below(keys));
      auto lb = o.lower_bound({probe, -1});
      const Item* got = t.lower_bound(probe);
      ok = ok && (lb == o.end() ? !got : got && got->k == lb->first && got->seq == lb->second);
    }
    CHECK(ok);
    for (auto& x : it) {
      if (T::linked(x)) t.remove(x);
    }
    CHECK(t.empty());
  }
}

// ---------------------------------------------------------------- the timer queue

TEST(timer_queue) {
  prim::TimerQueue q;
  prim::Timer a, b, c, d;
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
  CHECK(!q.due(1000));
  q.arm(a, 300);
  q.arm(b, 100);
  q.arm(c, 200);
  q.arm(d, 200);  // equal: after c
  CHECK(prim::TimerQueue::armed(a));
  CHECK_EQ(q.next(), 100u);
  CHECK(!q.due(99));
  CHECK_EQ(q.due(100), &b);
  CHECK(!prim::TimerQueue::armed(b));
  q.arm(a, 50);  // re-armed earlier
  CHECK_EQ(q.due(1000), &a);
  q.cancel(c);
  q.cancel(c);  // not armed: left alone
  CHECK_EQ(q.due(1000), &d);
  CHECK(!q.due(1000));
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}

TEST(timer_queue_random) {
  prim::TimerQueue q;
  static prim::Timer t[200];
  check::Rng rng(9);
  uint64_t now = 0;
  bool ok = true;
  for (int op = 0; op < 20000 && ok; ++op) {
    prim::Timer& x = t[rng.below(200)];
    if (rng.below(3)) {
      q.arm(x, now + rng.below(1000));
    } else {
      q.cancel(x);
    }
    now += rng.below(20);
    uint64_t last = 0;
    for (prim::Timer* d; (d = q.due(now));) {
      ok = ok && d->at <= now && d->at >= last;  // due, and in order
      last = d->at;
    }
    uint64_t earliest = prim::TimerQueue::kNever;
    for (auto& y : t) {
      if (prim::TimerQueue::armed(y)) {
        ok = ok && y.at > now;
        earliest = y.at < earliest ? y.at : earliest;
      }
    }
    ok = ok && q.next() == earliest;
  }
  CHECK(ok);
}

TEST(rbtree_size_is_a_walk) {
  T t;
  CHECK_EQ(t.size(), 0u);
  Item it[7];
  for (int i = 0; i < 7; ++i) {
    it[i].k = 7 - i;
    t.insert(it[i]);
  }
  CHECK_EQ(t.size(), 7u);
  t.remove(it[3]);
  CHECK_EQ(t.size(), 6u);
}

namespace {
struct Tick;
struct Tock;
// Two timers in one object: the owner derives from a hook for each, and a
// fire casts back through its hook. No offsetof: an owner need not be
// standard layout.
struct Owner : prim::TimerHook<Tick>, prim::TimerHook<Tock> {
  int fired = 0;
  uint64_t last = 0;
  prim::TimerQueue* q = nullptr;
  prim::Timer& tick() { return static_cast<prim::TimerHook<Tick>&>(*this); }
  prim::Timer& tock() { return static_cast<prim::TimerHook<Tock>&>(*this); }
};
void on_tick(prim::Timer& t) {
  Owner* o = &static_cast<Owner&>(static_cast<prim::TimerHook<Tick>&>(t));
  ++o->fired;
  o->last = t.at;
  if (o->fired == 1) o->q->arm(t, t.at + 10);  // re-armed from within: fires again if still due
}
void on_tock(prim::Timer& t) {
  Owner* o = &static_cast<Owner&>(static_cast<prim::TimerHook<Tock>&>(t));
  o->fired += 100;
  o->q->cancel(o->tick());  // a fire may cancel another
}
}  // namespace

TEST(timer_queue_runs_what_is_due_and_fires_it) {
  prim::TimerQueue q;
  Owner a, b;
  a.q = b.q = &q;
  a.tick().fire = on_tick;
  a.tock().fire = on_tock;
  b.tick().fire = on_tick;
  prim::Timer silent;  // no fire: only disarmed
  q.arm(a.tick(), 100);
  q.arm(b.tick(), 100);
  q.arm(silent, 100);
  q.arm(a.tock(), 150);
  CHECK_EQ(q.run(50), 0u);
  CHECK_EQ(q.run(105), 3u);  // a.tick, b.tick (re-armed to 110, not due), silent
  CHECK(a.fired == 1 && b.fired == 1 && a.last == 100);
  CHECK(prim::TimerQueue::armed(a.tick()) && !prim::TimerQueue::armed(silent));
  CHECK_EQ(q.run(200), 3u);  // a.tick at 110, b.tick at 110, then a.tock cancels a.tick (already fired)
  CHECK(a.fired == 102 && b.fired == 2 && a.last == 110);
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}
