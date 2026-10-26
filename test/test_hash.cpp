#include <cstring>
#include <map>

#include "check.h"
#include "prim/hash.h"
#include "prim/siphash.h"

// The keyed hash against the reference's vectors, and the hash table: each
// operation, growth, growth that fails, an object in two tables, and a
// seeded run against std::map.

namespace {

struct ById;
struct ByName;
struct Obj : prim::HashHook<ById>, prim::HashHook<ByName> {
  uint64_t id = 0;
  char name[8] = {};
};

struct IdTr {
  using Key = uint64_t;
  static Key key(const Obj& o) { return o.id; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

// Every key in one bucket: the chains are all there is.
struct CollideTr : IdTr {
  static uint32_t hash(const Key&) { return 7; }
};

struct NameTr {
  using Key = const char*;
  static Key key(const Obj& o) { return o.name; }
  static uint32_t hash(const Key& k) { return prim::hash32(k, uint32_t(std::strlen(k))); }
  static bool eq(const Key& a, const Key& b) { return std::strcmp(a, b) == 0; }
};

}  // namespace

TEST(siphash_matches_the_reference_vectors) {
  // vectors_hsip32 of github.com/veorq/SipHash: key 00..07, message 00..len-1.
  static const uint32_t want[16] = {
      0x5b9f35a9, 0xb85a4727, 0x03a662fa, 0x04e7fe8a, 0x89466e2a, 0x69b6fac5, 0x23fc6358, 0xc563cf8b,
      0x8f84b8d0, 0x79e706f8, 0x3479b094, 0x50300808, 0x2f87f057, 0xff63e677, 0x7cf8ffd6, 0x972bfe74};
  uint8_t key[8], msg[64];
  for (int i = 0; i < 8; ++i) key[i] = uint8_t(i);
  for (int i = 0; i < 64; ++i) msg[i] = uint8_t(i);
  bool ok = true;
  for (uint32_t len = 0; len < 16; ++len) ok = ok && prim::halfsiphash(msg, len, key) == want[len];
  CHECK(ok);
  // The table's key is the one seeded.
  prim::hash_seed(key);
  CHECK_EQ(prim::hash32(msg, 5), want[5]);
  const uint8_t zero[8] = {};
  prim::hash_seed(zero);
  CHECK(prim::hash32(msg, 5) != want[5]);
}

TEST(hash_each_operation) {
  check::Heap h;
  prim::Hash<Obj, IdTr, ById> t(h.a);
  Obj o[3];
  CHECK(!t.find(1));
  CHECK(!t.remove(o[0]));  // no buckets yet
  for (int i = 0; i < 3; ++i) {
    o[i].id = 10 + i;
    CHECK(t.insert(o[i]));
  }
  CHECK_EQ(t.size(), 3u);
  CHECK_EQ(t.find(11), &o[1]);
  CHECK(!t.find(99));
  CHECK(t.remove(o[1]));
  CHECK(!t.remove(o[1]));
  CHECK(!t.find(11));
  int seen = 0;
  t.each([&](Obj& x) {
    seen += int(x.id);
    t.remove(x);  // removing what it is given is allowed
  });
  CHECK_EQ(seen, 10 + 12);
  CHECK_EQ(t.size(), 0u);
}

TEST(hash_grows_and_survives_failing_growth) {
  check::Heap h;
  {
    check::Failing f(h.a, 0);  // the first bucket array fails
    prim::Hash<Obj, IdTr, ById> t(f);
    Obj o;
    CHECK(!t.insert(o));
    CHECK_EQ(t.size(), 0u);
  }
  check::Failing f(h.a, 1, true);  // the first succeeds, every later growth fails
  prim::Hash<Obj, IdTr, ById> t(f);
  Obj o[40];
  bool ok = true;
  for (int i = 0; i < 40; ++i) {
    o[i].id = uint64_t(i) * 0x100000001ull;
    ok = ok && t.insert(o[i]);
  }
  CHECK(ok);
  CHECK_EQ(t.buckets(), 4u);  // never grew: long chains, all still found
  for (int i = 0; i < 40; ++i) ok = ok && t.find(o[i].id) == &o[i];
  CHECK(ok);
  check::Heap h2;
  prim::Hash<Obj, IdTr, ById> g(h2.a);
  for (int i = 0; i < 40; ++i) g.insert(o[i]);
  CHECK_EQ(g.buckets(), 32u);  // two an object: 4, 8, 16, 32
}

TEST(hash_an_object_in_two_tables_and_one_bucket) {
  check::Heap h;
  prim::Hash<Obj, CollideTr, ById> by_id(h.a);
  prim::Hash<Obj, NameTr, ByName> by_name(h.a);
  Obj o[6];
  for (int i = 0; i < 6; ++i) {
    o[i].id = uint64_t(i);
    o[i].name[0] = char('a' + i);
    by_id.insert(o[i]);
    by_name.insert(o[i]);
  }
  CHECK(by_id.remove(o[0]));  // from the middle and the ends of one chain
  CHECK(by_id.remove(o[5]));
  CHECK(by_id.remove(o[3]));
  CHECK(!by_id.find(3) && by_id.find(4) == &o[4]);
  CHECK_EQ(by_name.find("d"), &o[3]);  // still in the other table
}

TEST(hash_random_against_std) {
  check::Heap h;
  prim::Hash<Obj, IdTr, ById> t(h.a);
  std::map<uint64_t, Obj*> o;
  static Obj pool[256];
  check::Rng rng(3);
  bool ok = true;
  for (int op = 0; op < 20000 && ok; ++op) {
    Obj& x = pool[rng.below(256)];
    if (o.count(x.id) && o[x.id] == &x) {
      ok = t.remove(x);
      o.erase(x.id);
    } else if (!o.count(x.id)) {
      ok = t.insert(x);
      o[x.id] = &x;
    }
    const uint64_t probe = rng.below(512);
    auto it = o.find(probe);
    ok = ok && t.find(probe) == (it == o.end() ? nullptr : it->second) && t.size() == o.size();
    if (op % 1000 == 999) {
      for (auto& p : pool) {
        if (!o.count(p.id) || o[p.id] != &p) p.id = rng.below(512);
      }
    }
  }
  CHECK(ok);
}

TEST(hash_several_objects_may_share_a_key) {
  check::Heap h;
  prim::Hash<Obj, CollideTr, ById> t(h.a);  // one bucket: other keys sit between
  Obj o[5];
  for (int i = 0; i < 5; ++i) {
    o[i].id = uint64_t(i % 2);  // ids 0 1 0 1 0
    t.insert(o[i]);
  }
  Obj* x = t.find(0);  // the most recently inserted first
  CHECK_EQ(x, &o[4]);
  x = t.find_next(*x);
  CHECK_EQ(x, &o[2]);
  x = t.find_next(*x);
  CHECK_EQ(x, &o[0]);
  CHECK(!t.find_next(*x));
  CHECK(t.remove(o[2]));
  CHECK_EQ(t.find_next(o[4]), &o[0]);
  CHECK_EQ(t.find_next(o[3]), &o[1]);  // id 1: o[1], inserted before it, is further down the chain
  CHECK(!t.find_next(o[1]));
}
