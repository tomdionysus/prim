#include <cstring>
#include <initializer_list>

#include "check.h"
#include "prim/vec.h"

// The arena: pmalloc's own cases, one for each of its fixes, every way a free
// merges, every check, and a seeded run of 20,000 calls checked after each.

using prim::Arena;
using Word = Arena::Word;
using Free = Arena::Free;

namespace {

constexpr uint32_t H = Arena::kHeader;
constexpr uint32_t A = Arena::kAlign;
constexpr uint32_t M = Arena::kMinStride;

uint32_t S(uint32_t n) { return Arena::capacity_for(n) + H; }  // the stride of a block for n
uint32_t cap_of(const Free* f) { return Arena::capacity_of(f->head); }
bool in(const void* p, const check::Buffer& b, uint32_t len) {
  const uintptr_t x = reinterpret_cast<uintptr_t>(p), lo = reinterpret_cast<uintptr_t>(b.buf);
  return x >= lo && x < lo + len;
}
Word* foot_of(const Free* f) {
  return reinterpret_cast<Word*>(reinterpret_cast<char*>(const_cast<Free*>(f)) + cap_of(f));
}
Word* after(const Free* f) {
  return reinterpret_cast<Word*>(reinterpret_cast<char*>(const_cast<Free*>(f)) + H + cap_of(f));
}

// An arena over a buffer whose block space, from the first head to the
// sentinel, is exactly `space`: an aligned start costs one alignment.
struct Small : check::Buffer {
  static constexpr uint32_t kMax = 65536;
  Arena a;
  uint32_t space;
  explicit Small(uint32_t s = kMax - A) : Buffer(kMax), space(s) { a.add(buf, s + A); }
};

// Walks an area block by block to its sentinel: heads aligned, each block's
// mark of the one before it right, a free block on the list with its foot
// right and an allocated one not.
bool tiles(const Arena& a, const void* area, uint32_t len) {
  const uintptr_t start = reinterpret_cast<uintptr_t>(area);
  const Word* h = reinterpret_cast<const Word*>(((start + H + A - 1) & ~uintptr_t(A - 1)) - H);
  const Word* s = reinterpret_cast<const Word*>(((start + len) & ~uintptr_t(A - 1)) - H);
  bool prev_used = true;
  while (h < s) {
    if ((reinterpret_cast<uintptr_t>(h) + H) % A) return false;
    const uint32_t c = Arena::capacity_of(*h);
    const bool used = *h & Arena::kUsed;
    if (bool(*h & Arena::kPrevUsed) != prev_used) return false;
    bool listed = false;
    for (prim::Mem k : {prim::Mem::Bulk, prim::Mem::Fast}) {
      for (const Free* f = a.free_list(k); f; f = f->next) listed = listed || &f->head == h;
    }
    if (used == listed) return false;
    if (!used && *reinterpret_cast<const Word*>(reinterpret_cast<const char*>(h) + c) != c) return false;
    prev_used = used;
    h = reinterpret_cast<const Word*>(reinterpret_cast<const char*>(h) + H + c);
  }
  return h == s && (*s & Arena::kUsed) && bool(*s & Arena::kPrevUsed) == prev_used;
}
bool tiles(const Small& s) { return tiles(s.a, s.buf, s.space + A); }

}  // namespace

TEST(arena_capacity_for) {
  CHECK_EQ(Arena::capacity_for(0) + H, M);
  CHECK_EQ(Arena::capacity_for(M - H), M - H);
  CHECK_EQ(Arena::capacity_for(M - H + 1) + H, M + A);
  const uint32_t c = Arena::capacity_for(1000);
  CHECK(c >= 1000 && c < 1000 + A && (c + H) % A == 0);
  CHECK_EQ(Arena::capacity_for(0xFFFFFFFFu), 0u);
}

// ---------------------------------------------------------------- pmalloc's cases

TEST(arena_pmalloc_alloc_size_free) {
  Small s;
  const uint32_t len[6] = {150, 256, 512, 100, 1024, 65536};
  void* m[6];
  for (int i = 0; i < 6; ++i) m[i] = s.a.alloc(len[i]);
  CHECK(m[5] == nullptr);
  for (int i = 0; i < 5; ++i) CHECK_EQ(s.a.size_of(m[i]), Arena::capacity_for(len[i]));
  for (int i = 0; i < 5; ++i) s.a.free(m[i]);
  CHECK_EQ(s.a.check(), 0);
  CHECK_EQ(s.a.blocks(), 1u);
  CHECK(tiles(s));
}

TEST(arena_pmalloc_fragmentation) {
  Small s(7 * S(512) + S(320));  // room for exactly the eight
  void* m[8];
  for (int i = 0; i < 8; ++i) CHECK((m[i] = s.a.alloc(i < 7 ? 512 : 320)) != nullptr);
  CHECK_EQ(s.a.free_bytes(), 0u);
  for (int i : {1, 3, 5}) s.a.free(m[i]);
  CHECK(s.a.alloc(1024) == nullptr);  // three holes, none big enough
  CHECK_EQ(s.a.largest_free(), Arena::capacity_for(512));
  for (int i : {0, 2, 4, 6, 7}) s.a.free(m[i]);
  CHECK_EQ(s.a.blocks(), 1u);
  CHECK_EQ(s.a.free_bytes(), s.space - H);
  CHECK_EQ(s.a.check(), 0);
}

TEST(arena_pmalloc_realloc_larger_elsewhere) {
  Small s;
  void* m[4];
  for (int i = 0; i < 4; ++i) m[i] = s.a.alloc(100 * (i + 1));
  std::memset(m[0], 0x42, 100);
  void* r = s.a.resize(m[0], 250);  // m[1] is after it: elsewhere, the contents with it
  CHECK(r && r != m[0]);
  CHECK(s.a.size_of(r) >= 250);
  CHECK_EQ(static_cast<char*>(r)[99], 0x42);
  CHECK_EQ(s.a.check(), 0);
  CHECK(tiles(s));
}

TEST(arena_pmalloc_realloc_smaller_and_same) {
  Small s;
  void* m[3];
  for (int i = 0; i < 3; ++i) m[i] = s.a.alloc(100 * (i + 1));
  CHECK(s.a.resize(m[1], 150) == m[1]);  // smaller: the rest given back, between m[1] and m[2]
  CHECK_EQ(s.a.size_of(m[1]), Arena::capacity_for(150));
  CHECK(s.a.resize(m[2], 300) == m[2]);  // the same
  CHECK(s.a.resize(m[2], 299) == m[2]);  // too little over to be a block
  CHECK_EQ(s.a.size_of(m[2]), Arena::capacity_for(300));
  CHECK_EQ(s.a.check(), 0);
  CHECK(tiles(s));
}

TEST(arena_pmalloc_realloc_into_space_after) {
  Small s;
  const uint32_t len[4] = {100, 200, 300, 150};
  void* m[4];
  for (int i = 0; i < 4; ++i) m[i] = s.a.alloc(len[i]);
  CHECK(s.a.resize(m[3], 2048) == m[3]);  // the last: into the free rest
  CHECK_EQ(s.a.size_of(m[3]), Arena::capacity_for(2048));
  s.a.free(m[2]);
  CHECK(s.a.resize(m[1], 450) == m[1]);  // into the block freed after it, the rest given back
  CHECK_EQ(s.a.size_of(m[1]), Arena::capacity_for(450));
  CHECK_EQ(s.a.check(), 0);
  CHECK(tiles(s));
}

TEST(arena_pmalloc_realloc_no_space_left) {
  Small s(S(2048) + S(1024));
  void* a = s.a.alloc(2048);
  void* b = s.a.alloc(1024);
  CHECK(a && b && s.a.free_bytes() == 0);
  CHECK(s.a.resize(b, 2048) == nullptr);  // the sentinel after it, nowhere else
  CHECK_EQ(s.a.size_of(b), Arena::capacity_for(1024));  // left as it was
  CHECK_EQ(s.a.check(), 0);
}

// ---------------------------------------------------------------- one case per fix

TEST(arena_a_split_keeps_every_block_in_place) {
  Small s(8192);
  void* p[6];
  for (auto& x : p) x = s.a.alloc(200);
  s.a.free(p[1]);
  s.a.free(p[3]);
  CHECK(s.a.alloc(64) == p[1]);  // first fit: the lower hole, from its head
  CHECK_EQ(s.a.check(), 0);
  CHECK(tiles(s));
}

TEST(arena_a_rest_too_small_for_a_block_is_not_split) {
  const uint32_t extra = M > A ? A : 0;  // a rest of one alignment, or none when that is itself a block
  Small s(M + extra);
  void* p = s.a.alloc(M - H);
  CHECK(p && s.a.blocks() == 1 && s.a.free_bytes() == 0);
  CHECK_EQ(s.a.size_of(p), M + extra - H);
  CHECK(s.a.alloc(0) == nullptr);
  CHECK(tiles(s));
}

TEST(arena_fresh_blocks_carry_nothing_from_their_memory) {
  check::Buffer b(4096);
  std::memset(b.buf, 0xAB, 4096);
  Arena a;
  a.add(b.buf + 2048, 2048);  // the later area first
  a.add(b.buf, 1024);         // then one before it: the new head of the list
  CHECK_EQ(a.check(), 0);
  void* p = a.alloc(100);
  void* q = a.alloc(100);
  a.free(p);
  a.free(q);
  CHECK_EQ(a.check(), 0);
  CHECK_EQ(a.blocks(), 2u);
  CHECK(tiles(a, b.buf, 1024) && tiles(a, b.buf + 2048, 2048));
}

TEST(arena_every_pointer_is_aligned) {
  Small s(16384);
  bool aligned = true;
  for (uint32_t n = 1; n <= 100; ++n) {
    void* p = s.a.alloc(n);
    aligned = aligned && p && reinterpret_cast<uintptr_t>(p) % A == 0;
  }
  CHECK(aligned);
  CHECK(tiles(s));
  // An unaligned area is trimmed to the alignment at both ends.
  check::Buffer b(1024);
  Arena a;
  a.add(b.buf + 3, 1000);
  const uintptr_t start = reinterpret_cast<uintptr_t>(b.buf) + 3;
  const uintptr_t first = ((start + H + A - 1) & ~uintptr_t(A - 1)) - H;
  const uintptr_t last = (start + 1000) & ~uintptr_t(A - 1);
  CHECK_EQ(a.free_bytes(), uint32_t(last - H - first) - H);
  void* p = a.alloc(5);
  CHECK(p && reinterpret_cast<uintptr_t>(p) % A == 0);
}

TEST(arena_areas_stay_apart) {
  check::Buffer b(4096);
  Arena a;
  a.add(b.buf, 1024);
  a.add(b.buf + 2048, 1024);
  CHECK_EQ(a.blocks(), 2u);
  CHECK_EQ(a.free_bytes(), 2 * (1024 - A - H));
  a.add(b.buf + 1024, 1024);  // between them: a third area with its own sentinel, not a merge
  CHECK_EQ(a.blocks(), 3u);
  CHECK_EQ(a.largest_free(), 1024 - A - H);
  CHECK_EQ(a.check(), 0);
}

TEST(arena_too_small_or_absurd_areas_are_ignored) {
  check::Buffer b(256);
  Arena a;
  a.add(nullptr, 1024);
  a.add(b.buf, M + A - 1);                                 // one short of a block and its sentinel
  a.add(b.buf + 1, A - 1);                                 // nothing once aligned
  a.add(b.buf + A - 1, 2);                                 // its end before its first head
  a.add(reinterpret_cast<void*>(~uintptr_t(0) - 8), 64);   // wraps the address space
  a.add(reinterpret_cast<void*>(~uintptr_t(0) - 4), 2);    // no room to align at the top of it
  CHECK(a.free_bytes() == 0 && a.blocks() == 0);
  CHECK(a.alloc(1) == nullptr);
}

TEST(arena_resize_never_grows_past_its_area) {
  check::Buffer b(4096);
  std::memset(b.buf + 512, 0xCD, 512);  // between the areas: not ours
  Arena a;
  a.add(b.buf, 512);
  a.add(b.buf + 1024, 3072);
  const uint32_t whole = 512 - A - H;
  void* p = a.alloc(whole);  // the whole of the first area
  CHECK(p && static_cast<char*>(p) < b.buf + 512 && a.size_of(p) == whole);
  std::memset(p, 0x11, whole);
  void* q = a.resize(p, 1024);
  CHECK(q && q != p);  // the sentinel after it: moved
  bool kept = true, copied = true;
  for (int i = 512; i < 1024; ++i) kept = kept && static_cast<unsigned char>(b.buf[i]) == 0xCD;
  for (uint32_t i = 0; i < whole; ++i) copied = copied && static_cast<char*>(q)[i] == 0x11;
  CHECK(kept && copied);
  CHECK_EQ(a.check(), 0);
}

TEST(arena_resize_into_a_small_gap_leaves_the_next_block_alone) {
  Small s(4096);
  char* x = static_cast<char*>(s.a.alloc(64));
  void* gap = s.a.alloc(1);
  char* y = static_cast<char*>(s.a.alloc(64));
  std::memset(y, 0x5A, 64);
  s.a.free(gap);
  bool ok = true;
  for (uint32_t grow = 65; grow <= 64 + 2 * M; ++grow) {  // into the gap while it fits, then elsewhere
    x = static_cast<char*>(s.a.resize(x, grow));
    ok = ok && x && s.a.check() == 0;
  }
  bool intact = true;
  for (int i = 0; i < 64; ++i) intact = intact && static_cast<unsigned char>(y[i]) == 0x5A;
  CHECK(ok && intact);
  CHECK(tiles(s));
}

TEST(arena_bad_frees_and_absurd_sizes_are_refused) {
  Small s(4096);
  void* p = s.a.alloc(100);
  void* q = s.a.alloc(0);  // a block of its own
  CHECK(q && q != p && s.a.size_of(q) == Arena::capacity_for(0));
  s.a.free(p);
  s.a.free(p);
  CHECK(s.a.resize(p, 50) == nullptr);
  CHECK_EQ(s.a.bad_frees(), 2u);
  CHECK(s.a.alloc(0xFFFFFFFFu) == nullptr);
  CHECK(s.a.resize(q, 0xFFFFFFFFu) == nullptr);
  CHECK_EQ(s.a.size_of(p), 0u);  // before anything can take its block again
  CHECK(s.a.resize(nullptr, 8) != nullptr);
  CHECK_EQ(s.a.size_of(nullptr), 0u);
  s.a.free(nullptr);
  CHECK_EQ(s.a.check(), 0);
}

// ---------------------------------------------------------------- merging, checking, churn

TEST(arena_a_free_merges_with_either_neighbour_or_both) {
  Small s(4096);
  void* m[4];
  for (auto& x : m) x = s.a.alloc(64);  // m0 m1 m2 m3, then the rest
  s.a.free(m[2]);                      // neither neighbour free: on the list by itself
  CHECK_EQ(s.a.blocks(), 5u);
  s.a.free(m[3]);  // both free: m2, m3 and the rest become one
  CHECK_EQ(s.a.blocks(), 3u);
  s.a.free(m[0]);  // nothing before, m1 after: by itself, so still three blocks
  CHECK_EQ(s.a.blocks(), 3u);
  s.a.free(m[1]);  // both: everything is one block again
  CHECK_EQ(s.a.blocks(), 1u);
  void* a = s.a.alloc(64);
  void* b = s.a.alloc(64);
  void* c = s.a.alloc(64);  // a b c, then the rest
  s.a.free(c);              // the rest after it only
  CHECK_EQ(s.a.blocks(), 3u);
  void* big = s.a.alloc(1000);          // from the head of that block: a b big, the rest
  CHECK(s.a.resize(big, 100) == big);  // the rest given back merges with the free rest after it
  CHECK_EQ(s.a.blocks(), 4u);
  s.a.free(a);  // nothing before, b after: by itself
  s.a.free(b);  // a before it only; big after is allocated
  CHECK_EQ(s.a.blocks(), 3u);
  s.a.free(big);  // both
  CHECK_EQ(s.a.blocks(), 1u);
  CHECK_EQ(s.a.free_bytes(), s.space - H);
  CHECK_EQ(s.a.check(), 0);
  CHECK(tiles(s));
}

TEST(arena_resize_with_only_free_space_before_it) {
  Small s(S(256) + S(512));
  void* a = s.a.alloc(256);
  void* b = s.a.alloc(512);
  CHECK(a && b && s.a.free_bytes() == 0);
  s.a.free(a);
  CHECK(s.a.resize(b, 1024) == nullptr);  // the sentinel after it; the hole before it too small
  CHECK_EQ(s.a.check(), 0);
}

TEST(arena_check_finds_each_kind_of_damage) {
  for (int kind = 1; kind <= 10; ++kind) {
    if (kind == 10 && A <= 8) continue;  // at 8, every capacity is a block's size
    Small s(4096);
    void* m[5];
    for (auto& x : m) x = s.a.alloc(64);
    s.a.free(m[1]);
    s.a.free(m[3]);  // the list: m1, m3, the rest
    Free* first = const_cast<Free*>(s.a.free_list(prim::Mem::Bulk));
    Free* second = first->next;
    Free* third = second->next;
    int want = kind;
    switch (kind) {
      case 1: first->next = reinterpret_cast<Free*>(reinterpret_cast<char*>(second) + 1); break;
      case 2: second->head |= Arena::kUsed; break;
      case 3: second->head |= Arena::kFast; break;  // marked fast on the bulk list
      case 4: third->prev = first; break;
      case 5:  // third before second
        first->next = third, third->prev = first, third->next = second, second->prev = third, second->next = nullptr;
        break;
      case 6: *foot_of(second) = 0; break;
      case 7: {  // the rest cut short behind a false sentinel: the free total no longer adds up
        const uint32_t c = cap_of(third) - A;
        third->head = Arena::pack(c) | (third->head & Arena::kPrevUsed);
        *foot_of(third) = c;
        *after(third) = Arena::kUsed;
        break;
      }
      case 8: *after(second) |= Arena::kPrevUsed, want = 6; break;      // the next block's mark
      case 9: second->head = Arena::pack(M - H - A) | Arena::kPrevUsed, want = 2; break;  // too small for a block
      case 10: second->head -= 8, want = 2; break;                       // not a block's size
    }
    CHECK_EQ(s.a.check(), want);
  }
}

TEST(arena_check_finds_free_blocks_left_unmerged) {
  Small s(4096);
  void* m[3];
  for (auto& x : m) x = s.a.alloc(64);
  s.a.free(m[0]);
  s.a.free(m[2]);  // the list: m0, then m2 merged with the rest
  Free* first = const_cast<Free*>(s.a.free_list(prim::Mem::Bulk));
  const uint32_t c = uint32_t(reinterpret_cast<char*>(first->next) - reinterpret_cast<char*>(first)) - H;
  first->head = Arena::pack(c) | (first->head & Arena::kPrevUsed);  // m0 now reaches the next free block
  *foot_of(first) = c;
  first->next->head &= ~Arena::kPrevUsed;  // which is marked as it would be
  CHECK_EQ(s.a.check(), 5);
}

// ---------------------------------------------------------------- two kinds of memory

TEST(arena_fast_is_a_requirement_and_bulk_a_preference) {
  using prim::Mem;
  check::Buffer fast(4096), bulk(8192);
  Arena a;
  a.add(fast.buf, 4096, Mem::Fast);
  a.add(bulk.buf, 8192);
  void* x = a.alloc(100);  // bulk, asked for nothing in particular
  void* y = a.alloc(100, Mem::Fast);
  CHECK(in(x, bulk, 8192) && in(y, fast, 4096));
  CHECK_EQ(a.stats(Mem::Bulk).used, Arena::capacity_for(100));
  CHECK_EQ(a.stats(Mem::Fast).used, Arena::capacity_for(100));
  void* w = a.alloc(200, Mem::Fast);
  void* grown = a.resize(y, 500);  // w is after it: elsewhere, and in fast memory still
  CHECK(grown && grown != y && in(grown, fast, 4096));
  CHECK(a.alloc(3000, Mem::Fast) != nullptr);
  CHECK(a.alloc(2000, Mem::Fast) == nullptr);  // no fast left that big: refused, with bulk to spare
  CHECK_EQ(a.failures(), 1u);
  CHECK(in(a.alloc(2000), bulk, 8192));
  void* rest = a.alloc(a.stats(Mem::Bulk).largest);  // all of bulk
  CHECK(rest != nullptr);
  void* z = a.alloc(50);  // no bulk: fast will do
  CHECK(in(z, fast, 4096));
  CHECK_EQ(a.failures(), 1u);
  CHECK_EQ(a.check(), 0);
  CHECK(tiles(a, fast.buf, 4096) && tiles(a, bulk.buf, 8192));
  for (void* p : {x, w, grown, rest, z}) a.free(p);
  CHECK(a.stats(Mem::Fast).free > a.stats(Mem::Fast).least);  // the low-water mark stays
  CHECK(a.stats(Mem::Bulk).least < Arena::capacity_for(100));
  CHECK_EQ(a.check(), 0);
}

TEST(arena_a_board_with_only_fast_memory_ignores_the_kind) {
  check::Heap h;  // all fast, as a V3 adds it
  void* p = h.a.alloc(100);
  void* q = h.a.alloc(100, prim::Mem::Fast);
  CHECK(p && q);
  CHECK_EQ(h.a.stats(prim::Mem::Bulk).free, 0u);
  CHECK_EQ(h.a.stats(prim::Mem::Bulk).largest, 0u);
  CHECK_EQ(h.a.stats(prim::Mem::Fast).used, 2 * Arena::capacity_for(100));
  CHECK_EQ(h.a.failures(), 0u);
  CHECK_EQ(h.a.largest_free(), h.a.stats(prim::Mem::Fast).largest);  // the one kind's is the whole's
  CHECK(h.a.resize(p, 5000) != nullptr);  // moved, within the one kind
  CHECK_EQ(h.a.check(), 0);
}

TEST(account_gives_a_container_its_kind_and_counts_what_its_owner_holds) {
  using prim::Mem;
  check::Buffer fast(4096), bulk(8192);
  Arena a;
  a.add(fast.buf, 4096, Mem::Fast);
  a.add(bulk.buf, 8192);
  prim::Account hot(a, Mem::Fast);
  prim::Account cold(a);
  prim::Vec<int> v(hot, 100);
  prim::Vec<int> u(cold, 100);
  CHECK(v.push(1) && u.push(1));
  CHECK(in(v.begin(), fast, 4096) && in(u.begin(), bulk, 8192));
  CHECK(hot.objects() == 1 && hot.bytes() == a.size_of(v.begin()) && cold.objects() == 1);
  void* p = cold.alloc(10, Mem::Fast);  // an explicit ask for fast passes through
  CHECK(in(p, fast, 4096));
  CHECK_EQ(cold.objects(), 2u);
  CHECK_EQ(hot.size_of(p), a.size_of(p));
  CHECK_EQ(hot.stats(Mem::Fast).used, a.stats(Mem::Fast).used);
  const uint32_t before = cold.bytes();
  void* q = cold.resize(p, 300);  // moved: the account follows the block, not the pointer
  CHECK(q && in(q, fast, 4096));
  CHECK_EQ(cold.bytes(), a.size_of(u.begin()) + a.size_of(q));
  CHECK(cold.bytes() > before);
  CHECK_EQ(cold.peak(), cold.bytes());
  CHECK(cold.resize(q, 0xFFFFFFFFu) == nullptr);  // refused: nothing changes
  CHECK_EQ(cold.objects(), 2u);
  cold.free(q);
  cold.free(q);                                   // a bad free counts nothing
  cold.free(nullptr);
  CHECK(cold.objects() == 1 && cold.bytes() == a.size_of(u.begin()));
  CHECK(cold.peak() > cold.bytes());
  CHECK(cold.alloc(5000, Mem::Fast) == nullptr);  // a refusal counts nothing
  CHECK_EQ(cold.objects(), 1u);
  void* s = cold.alloc(8);  // below the peak still
  CHECK(s && cold.peak() > cold.bytes() && cold.objects() == 2);
  cold.free(s);
  CHECK_EQ(a.check(), 0);
}

TEST(arena_random_churn_keeps_every_invariant) {
  constexpr uint32_t kSpace = 16384;
  Small s(kSpace);
  struct Live {
    unsigned char* p = nullptr;
    uint32_t n = 0;
    unsigned char tag = 0;
  } live[64];
  check::Rng rng(1);
  auto intact = [](const Live& l, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i)
      if (l.p[i] != static_cast<unsigned char>(l.tag + i)) return false;
    return true;
  };
  bool ok = true;
  uint32_t refused = 0;
  for (int op = 0; op < 20000 && ok; ++op) {
    Live& l = live[rng.below(64)];
    const uint32_t r = rng.below(100);
    const uint32_t n = rng.below(10) == 0 ? rng.below(4000) : rng.below(300);
    if (!l.p) {
      if (!(l.p = static_cast<unsigned char*>(s.a.alloc(n)))) {
        ++refused;
        continue;
      }
      l.n = n;
      l.tag = static_cast<unsigned char>(rng());
    } else if (r < 60) {
      ok = intact(l, l.n);
      s.a.free(l.p);
      l = Live{};
      continue;
    } else {
      auto* q = static_cast<unsigned char*>(s.a.resize(l.p, n));
      if (!q) {
        ++refused;
        ok = intact(l, l.n);
        continue;
      }
      l.p = q;
      ok = intact(l, l.n < n ? l.n : n);
      l.n = n;
    }
    for (uint32_t i = 0; i < l.n; ++i) l.p[i] = static_cast<unsigned char>(l.tag + i);
    ok = ok && s.a.check() == 0 && tiles(s);
    for (const Live& x : live) {
      ok = ok && (!x.p || (reinterpret_cast<uintptr_t>(x.p) % A == 0 && s.a.size_of(x.p) >= x.n &&
                           s.a.size_of(x.p) < x.n + 2 * M));  // a rest too small to split, over the rounding
    }
  }
  CHECK(ok);
  CHECK(refused > 0);
  for (Live& l : live) {
    CHECK(!l.p || intact(l, l.n));
    s.a.free(l.p);
  }
  CHECK_EQ(s.a.blocks(), 1u);
  CHECK_EQ(s.a.largest_free(), kSpace - H);
  CHECK_EQ(s.a.used_bytes(), 0u);
  CHECK_EQ(s.a.bad_frees(), 0u);
}
