#include <cstring>

#include "check.h"
#include "prim/list.h"
#include "prim/pool.h"
#include "prim/rbtree.h"
#include "prim/ring.h"
#include "prim/secure.h"
#include "prim/timer.h"
#include "prim/trie.h"
#include "prim/vec.h"

// What the library refuses and why: a pointer the arena did not give out, an
// object put in a container twice, an index past the end, a pool object put
// back twice; and the timer queue's bound on run(), the fast-memory reserve
// and the pool as an allocator.

using prim::Arena;
using Word = Arena::Word;

namespace {

constexpr uint32_t H = Arena::kHeader;
constexpr uint32_t A = Arena::kAlign;
Word* head_of(void* p) { return reinterpret_cast<Word*>(static_cast<char*>(p) - H); }

}  // namespace

// ---------------------------------------------------------------- the arena

TEST(arena_refuses_pointers_it_did_not_give_out) {
  check::Buffer b(8192);
  Arena a;
  a.add(b.buf, 8192);
  void* x = a.alloc(64);
  void* y = a.alloc(64);
  void* z = a.alloc(64);
  void* w = a.alloc(64);
  alignas(64) static unsigned char outside[128];
  uint32_t bad = 0;
  // Misaligned, outside the memory, and inside it but not allocated.
  CHECK_ASSERTS(1, a.free(static_cast<char*>(x) + 1));
  CHECK_ASSERTS(1, a.free(outside + 64));
  CHECK_EQ(a.size_of(outside + 64), 0u);
  CHECK_ASSERTS(1, CHECK(a.resize(outside + 64, 8) == nullptr));
  bad += 3;
  // A head damaged each way an overflow of the block before it could: each
  // refused, then put right.
  auto damaged = [&](void* p, Word* at, Word to) {
    const Word was = *at;
    *at = to;
    CHECK_ASSERTS(1, a.free(p));
    CHECK_EQ(a.size_of(p), 0u);
    *at = was;
    ++bad;
  };
  damaged(y, head_of(y), *head_of(y) | Arena::kFast);                    // marked fast, in bulk memory
  damaged(y, head_of(y), Arena::pack(0x100000) | Arena::kUsed | Arena::kPrevUsed);  // reaching past the memory
  damaged(y, head_of(z), *head_of(z) & ~Arena::kPrevUsed);              // the next block does not know it
  damaged(x, head_of(x), *head_of(x) & ~Arena::kPrevUsed);              // a free block before the first
  a.free(y);                                                            // now z has a free block before it
  damaged(z, head_of(z) - 1, Word(0x100000));                           // a foot reaching out of the memory
  damaged(z, head_of(z) - 1, Word(Arena::capacity_for(64) + H + Arena::capacity_for(64)));  // a foot to x, allocated
  damaged(z, head_of(z) - 1, Word(Arena::capacity_for(64) - A));        // a foot to no block's capacity
  CHECK_EQ(a.bad_frees(), bad);
  CHECK_EQ(a.check(), 0);
  for (void* p : {x, z, w}) a.free(p);
  CHECK_EQ(a.check(), 0);
  CHECK_EQ(a.used_bytes(), 0u);
}

TEST(arena_keeps_a_fast_reserve_from_bulk_requests) {
  using prim::Mem;
  check::Buffer fast(4096);
  Arena a;
  a.add(fast.buf, 4096, Mem::Fast);
  const uint32_t all = a.stats(Mem::Fast).free;
  a.reserve_fast(all - 512);
  CHECK_EQ(a.fast_reserve(), all - 512);
  void* p = a.alloc(256);  // bulk, met from fast: the reserve still stands
  CHECK(p != nullptr);
  CHECK(a.alloc(512) == nullptr);  // would cut into the reserve: refused, and counted
  CHECK(a.alloc(1u << 20) == nullptr);  // more than there is at all
  CHECK_EQ(a.failures(), 2u);
  void* q = a.alloc(512, Mem::Fast);  // fast itself may use it
  CHECK(q != nullptr);
  a.free(p);
  a.free(q);
  a.reserve_fast(0);
  CHECK(a.alloc(all) != nullptr);  // without a reserve, bulk may take it all
}

TEST(arena_finds_a_fit_by_class) {
  Arena a;
  check::Buffer b(16384);
  a.add(b.buf, 16384);
  // Holes of 48 and 100 in their classes, then a request for 90: not the
  // 48, whose class it shares in no way, nor too small a one in its own.
  void* h1 = a.alloc(48);
  void* k1 = a.alloc(8);
  void* h2 = a.alloc(80);
  void* k2 = a.alloc(8);
  void* h3 = a.alloc(100);
  void* k3 = a.alloc(8);
  a.free(h1);
  a.free(h2);
  a.free(h3);
  CHECK(a.alloc(90) == h3);  // its own class holds 80 and 100: the one that fits
  CHECK(a.alloc(70) == h2);  // its own class: the 80
  CHECK(a.alloc(16) == h1);  // a class below has the 48; the first one up with any
  for (void* p : {k1, k2, k3}) a.free(p);
  CHECK_EQ(a.check(), 0);
}

// ---------------------------------------------------------------- containers refuse misuse

namespace {
struct Node : prim::ListHook<>, prim::RbHook<> {
  int k = 0;
};
struct ByK {
  static int cmp(const Node& a, const Node& b) { return a.k - b.k; }
};
}  // namespace

TEST(list_refuses_what_would_corrupt_it) {
  prim::List<Node> l, m;
  Node a, b, c;
  l.push_back(a);
  CHECK_ASSERTS(1, l.push_back(a));
  CHECK_ASSERTS(1, m.push_front(a));    // on another list
  CHECK_ASSERTS(1, l.insert_after(b, c));  // b is on no list
  CHECK_ASSERTS(1, l.insert_after(a, a));  // a is on one
  CHECK_ASSERTS(1, prim::List<Node>::remove(b));
  CHECK_EQ(l.size(), 1u);
  m.move_front(b);  // from no list
  m.move_front(a);  // from another list
  CHECK(l.empty() && m.size() == 2 && m.front() == &a);
  while (m.pop_front()) {
  }
}

TEST(tree_refuses_what_would_corrupt_it) {
  prim::Tree<Node, ByK> t;
  Node a, b;
  t.insert(a);
  CHECK_ASSERTS(1, t.insert(a));
  CHECK_ASSERTS(1, t.remove(b));
  CHECK_EQ(t.size(), 1u);
  t.remove(a);
  CHECK(t.empty());
}

TEST(vec_and_ring_refuse_indexes_past_the_end) {
  check::Heap h;
  prim::Vec<int> v(h.a, 10);
  v.push(1);
  v.push(2);
  CHECK_ASSERTS(1, CHECK(!v.insert(3, 9)));
  CHECK_ASSERTS(1, v.erase(2));
  CHECK_ASSERTS(1, v.erase_swap(7));
  CHECK_EQ(v.size(), 2u);
  CHECK_ASSERTS(1, (void)v[2]);  // within the capacity, past the size
  CHECK(v.insert(2, 3) && v[2] == 3);  // at the end itself is a push
  prim::Ring<int> r(h.a, 8, false);
  r.push(1);
  CHECK_ASSERTS(1, (void)r[1]);
  CHECK(*r.oldest() == 1 && *r.newest() == 1);
}

// ---------------------------------------------------------------- the pool

TEST(pool_refuses_an_object_put_back_twice) {
  check::Heap h;
  prim::Pool p(h.a, 32, 8);
  void* x = p.get();
  void* y = p.get();
  CHECK(p.put(x));
  CHECK(!p.put(x));  // twice
  void* never = static_cast<char*>(y) + 3 * 32;  // an object of the slab that was never got
  CHECK(!p.put(never));
  void* g1 = p.get();
  void* g2 = p.get();
  CHECK(g1 != g2 && p.in_use() == 3);
  for (void* o : {y, g1, g2}) CHECK(p.put(o));
}

TEST(pool_is_an_allocator) {
  check::Heap h;
  prim::Pool p(h.a, 24, 4);
  prim::Alloc& a = p;
  void* x = a.alloc(20);
  CHECK(x && a.size_of(x) == 24);
  CHECK(a.alloc(25) == nullptr);  // more than an object
  CHECK(a.resize(x, 24) == x);     // in place, within the size
  CHECK(a.resize(x, 25) == nullptr);
  int stranger = 0;
  CHECK(a.resize(&stranger, 8) == nullptr);
  CHECK_EQ(a.size_of(&stranger), 0u);
  void* y = a.resize(nullptr, 8);
  CHECK(y && y != x);
  a.free(x);
  CHECK_EQ(a.size_of(x), 0u);  // back: no longer out
  a.free(nullptr);
  a.free(y);
  CHECK_EQ(a.stats(prim::Mem::Bulk).used, h.a.stats(prim::Mem::Bulk).used);
  CHECK_EQ(a.stats(prim::Mem::Fast).used, h.a.stats(prim::Mem::Fast).used);
}

TEST(trie_nodes_from_a_pool) {
  check::Heap h;
  const uint32_t before = h.a.used_bytes();
  {
    prim::Pool nodes(h.a, prim::Trie<int>::kNodeSize, 32);
    prim::Trie<int> t(nodes);
    int v = 0;
    check::Rng rng(21);
    for (int i = 0; i < 500; ++i) t.insert(uint64_t(rng()) << 32 | rng(), uint8_t(8 + rng.below(25)), v);
    CHECK_EQ(nodes.in_use(), t.nodes());
    const uint32_t blocks = h.a.blocks();
    CHECK(blocks < 2 + nodes.slabs() * 2);  // a block a slab, not a block a node
    CHECK(t.match(0) == nullptr || t.match(0) == &v);
  }
  CHECK_EQ(h.a.used_bytes(), before);
}

// ---------------------------------------------------------------- the timer queue

namespace {
prim::TimerQueue* g_q;
int g_fires;
prim::Timer* g_other;
void rearm_now(prim::Timer& t) {
  ++g_fires;
  g_q->arm(t, t.at);  // due again at once
}
void cancel_other(prim::Timer&) {
  ++g_fires;
  g_q->cancel(*g_other);
}
void rearm_other(prim::Timer&) {
  ++g_fires;
  g_q->arm(*g_other, 0);
}
void nested_run(prim::Timer&) {
  ++g_fires;
  g_q->run(1000);
}
void count(prim::Timer&) { ++g_fires; }
}  // namespace

TEST(timer_run_returns_however_its_fires_rearm) {
  static prim::TimerQueue q;
  g_q = &q;
  g_fires = 0;
  static prim::Timer t;
  t.fire = rearm_now;
  q.arm(t, 5);
  CHECK_EQ(q.run(5), 1u);  // once, not for ever
  CHECK(prim::TimerQueue::armed(t));
  CHECK_EQ(q.run(5), 1u);  // and again on the next run
  CHECK_EQ(g_fires, 2);
  q.cancel(t);
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}

TEST(timer_fires_may_cancel_and_rearm_what_is_pending) {
  static prim::TimerQueue q;
  g_q = &q;
  g_fires = 0;
  // a, b, c and e are all due and taken out together; a cancels c, between
  // b and e, so c never fires.
  static prim::Timer a, b, c, e;
  a.fire = cancel_other;
  b.fire = c.fire = e.fire = count;
  g_other = &c;
  q.arm(a, 10);
  q.arm(b, 20);
  q.arm(c, 22);
  q.arm(e, 25);
  CHECK_EQ(q.run(30), 3u);
  CHECK(!prim::TimerQueue::armed(c) && g_fires == 3);
  // f and d are due together; f re-arms d, the last of them, to 0: d goes
  // back to the tree and waits for the next run.
  static prim::Timer f, d;
  f.fire = rearm_other;
  d.fire = count;
  g_other = &d;
  q.arm(f, 100);
  q.arm(d, 110);
  CHECK_EQ(q.run(200), 1u);
  CHECK(prim::TimerQueue::armed(d) && q.next() == 0u);
  CHECK_EQ(q.run(200), 1u);
  CHECK_EQ(g_fires, 5);
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}

TEST(timer_run_inside_a_fire_takes_the_rest) {
  static prim::TimerQueue q;
  g_q = &q;
  g_fires = 0;
  static prim::Timer a, b, c;
  a.fire = nested_run;  // b and c are pending behind it
  b.fire = c.fire = count;
  q.arm(a, 1);
  q.arm(b, 2);
  q.arm(c, 3);
  CHECK_EQ(q.run(10), 1u);  // a; the run inside it fired b and c
  CHECK_EQ(g_fires, 3);
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}

namespace {
struct Seen {
  uint64_t next1, next2;
  prim::Timer *due0, *due1, *due2, *due3;
} g_seen;
prim::Timer *g_x, *g_late;
void probe(prim::Timer&) {
  ++g_fires;
  g_seen.next1 = g_q->next();   // b, pending, is earlier than anything in the tree
  g_seen.due0 = g_q->due(0);    // nothing due by 0
  g_q->arm(*g_x, 0);
  g_seen.next2 = g_q->next();   // the tree now has something earlier
  g_seen.due1 = g_q->due(5);    // and that comes first
  g_seen.due2 = g_q->due(5);    // then b, pending
  g_q->cancel(*g_late);
  g_seen.due3 = g_q->due(10);   // c, pending, with the tree empty
}
}  // namespace

TEST(timer_due_and_next_inside_a_fire) {
  static prim::TimerQueue q;
  g_q = &q;
  g_fires = 0;
  static prim::Timer a, b, c, x, late;
  g_x = &x;
  g_late = &late;
  a.fire = probe;
  b.fire = c.fire = count;
  q.arm(a, 1);
  q.arm(b, 2);
  q.arm(c, 3);
  q.arm(late, 5000);
  CHECK_EQ(q.run(10), 1u);  // only a fired: b and c were taken by due()
  CHECK(g_seen.next1 == 2 && g_seen.due0 == nullptr && g_seen.next2 == 0);
  CHECK(g_seen.due1 == &x && g_seen.due2 == &b && g_seen.due3 == &c);
  CHECK_EQ(g_fires, 1);
  CHECK_EQ(q.next(), prim::TimerQueue::kNever);
}

TEST(secure_zero_clears) {
  unsigned char key[32];
  std::memset(key, 0xA5, sizeof key);
  prim::secure_zero(key, sizeof key);
  bool zero = true;
  for (unsigned char c : key) zero = zero && c == 0;
  CHECK(zero);
}

TEST(pool_slabs_are_cut_by_their_bits_too) {
  check::Heap h;  // one slab of up to 64 KB
  prim::Pool p(h.a, 8, 10000);  // the bits for 8,191 objects push the slab past 16-bit offsets
  CHECK(p.per_slab() < 8191u && p.per_slab() > 8000u);
  void* x = p.get();
  CHECK(x && p.put(x));
}

TEST(arena_least_free_is_never_a_moment_inside_a_call) {
  check::Buffer b(65536);
  Arena a;
  a.add(b.buf, 65536);
  void* p = a.alloc(100);
  void* q = a.resize(p, 50);     // the rest given back, merged with the free space after it
  void* r = a.resize(q, 3000);   // grown in place over all of that, the rest given back again
  CHECK(r == p);
  const prim::Stats s = a.stats(prim::Mem::Bulk);
  CHECK_EQ(s.least, s.free);     // the least there has been is what is free now, not zero
  a.free(r);
}
