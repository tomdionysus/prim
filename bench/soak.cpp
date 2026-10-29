// A simulated day of a node's memory: frames from a pool, sessions coming and
// going in a table and a list with a timer each, buffers growing and being
// dropped, and a routing trie whose nodes come from a pool. Every simulated
// hour it prints, for each kind of memory, what is free, the largest block,
// how many free blocks there are and the longest size class, and at the end
// what the arena's calls cost. It fails if the arena's structures are ever
// wrong or anything is left allocated.
//
//   prim_soak [v3|v4]     v3: 192 KB, all fast; v4 (default): 96 KB fast and 1.5 MB bulk

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "prim/arena.h"
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/pool.h"
#include "prim/siphash.h"
#include "prim/timer.h"
#include "prim/trie.h"
#include "prim/vec.h"

namespace {

using prim::Mem;

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  uint32_t operator()() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return uint32_t(s >> 11);
  }
  uint32_t below(uint32_t n) { return (*this)() % n; }
} rng;

// The arena, timed.
struct Timed final : prim::Alloc {
  prim::Arena& a;
  uint64_t calls = 0, ns = 0;
  explicit Timed(prim::Arena& to) : a(to) {}
  template <class F>
  auto timed(F f) {
    const auto t0 = std::chrono::steady_clock::now();
    auto r = f();
    ns += uint64_t(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
    ++calls;
    return r;
  }
  void* alloc(uint32_t n, Mem want) override { return timed([&] { return a.alloc(n, want); }); }
  void* resize(void* p, uint32_t n) override { return timed([&] { return a.resize(p, n); }); }
  void free(void* p) override {
    timed([&] {
      a.free(p);
      return 0;
    });
  }
  uint32_t size_of(const void* p) const override { return a.size_of(p); }
  prim::Stats stats(Mem kind) const override { return a.stats(kind); }
};

struct ById;
struct Expiry;
struct Session : prim::ListHook<>, prim::HashHook<ById>, prim::TimerHook<Expiry> {
  uint32_t id;
  prim::Vec<uint8_t>* buf;
};
struct SessionById {
  using Key = uint32_t;
  static Key key(const Session& s) { return s.id; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

struct Frame : prim::TimerHook<> {
  uint8_t body[240];
};

struct Census {
  uint32_t blocks = 0, longest = 0;
};
Census census(const prim::Arena& a, Mem k) {
  Census c;
  for (int b = 0; b < prim::Arena::kBins; ++b) {
    uint32_t n = 0;
    for (const auto* f = a.free_list(k, b); f; f = f->next) ++n;
    c.blocks += n;
    c.longest = n > c.longest ? n : c.longest;
  }
  return c;
}

}  // namespace

int main(int argc, char** argv) {
  const bool v3 = argc > 1 && std::strcmp(argv[1], "v3") == 0;
  const uint32_t fast_len = v3 ? 192 * 1024 : 96 * 1024;
  const uint32_t bulk_len = v3 ? 0 : 1536 * 1024;
  void* fast_mem = std::malloc(fast_len);
  void* bulk_mem = bulk_len ? std::malloc(bulk_len) : nullptr;
  int rc = 0;
  {
    prim::Arena arena;
    arena.add(fast_mem, fast_len, Mem::Fast);
    if (bulk_mem) arena.add(bulk_mem, bulk_len, Mem::Bulk);
    arena.reserve_fast(v3 ? 0 : 16 * 1024);
    Timed timed(arena);
    {
    prim::Account fast(timed, Mem::Fast), bulk(timed, Mem::Bulk);

    const uint8_t seed[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    prim::hash_seed(seed);
    prim::Pool frames(fast, sizeof(Frame), 16);
    prim::Pool route_nodes(fast, prim::Trie<int>::kNodeSize, 32);
    prim::Trie<int> routes(route_nodes);
    prim::Hash<Session, SessionById, ById> by_id(bulk);
    prim::List<Session> lru;
    prim::TimerQueue timers;
    int route_value = 0;
    uint32_t next_id = 1, live_sessions = 0, refused = 0;
    uint64_t routes_keys[256] = {};
    uint8_t routes_lens[256] = {};

    static Timed* g_timed;
    static prim::Pool* g_frames;
    static prim::Hash<Session, SessionById, ById>* g_by_id;
    static uint32_t* g_live;
    g_timed = &timed;
    g_frames = &frames;
    g_by_id = &by_id;
    g_live = &live_sessions;
    (void)g_timed;

    const uint64_t kTick = 100;                 // ms
    const uint64_t kDay = 24ull * 3600 * 1000;  // ms
    std::printf("%s: fast %" PRIu32 " KB, bulk %" PRIu32 " KB, fast reserve %" PRIu32 "\n", v3 ? "v3" : "v4", fast_len / 1024, bulk_len / 1024,
                arena.fast_reserve());
    std::printf("%5s %8s %8s %6s %6s | %8s %8s %6s %6s | %5s %5s %6s %6s\n", "hour", "fastfree", "largest", "freeblk",
                "class", "bulkfree", "largest", "freeblk", "class", "sess", "frame", "route", "fails");
    for (uint64_t now = 0; now <= kDay && !rc; now += kTick) {
      // Frames: a few a tick, each held from a tick to five seconds.
      for (uint32_t n = rng.below(4); n--;) {
        auto* f = static_cast<Frame*>(frames.get());
        if (!f) {
          ++refused;
          continue;
        }
        new (f) Frame;
        f->fire = [](prim::Timer& t) { g_frames->put(&static_cast<Frame&>(static_cast<prim::TimerHook<>&>(t))); };
        timers.arm(*f, now + kTick * (1 + rng.below(50)));
      }
      // A session now and then, for five to thirty minutes, with a buffer.
      if (rng.below(40) == 0) {
        auto* s = static_cast<Session*>(bulk.alloc(sizeof(Session)));
        auto* v = s ? static_cast<prim::Vec<uint8_t>*>(bulk.alloc(sizeof(prim::Vec<uint8_t>))) : nullptr;
        if (!v) {
          bulk.free(s);
          ++refused;
        } else {
          new (s) Session;
          s->id = next_id++;
          s->buf = new (v) prim::Vec<uint8_t>(bulk, 4096);
          if (!by_id.insert(*s)) ++refused;
          lru.push_front(*s);
          ++live_sessions;
          s->fire = [](prim::Timer& t) {
            Session* s = &static_cast<Session&>(static_cast<prim::TimerHook<Expiry>&>(t));
            g_by_id->remove(*s);
            prim::List<Session>::remove(*s);
            prim::Alloc& a = *g_timed;
            s->buf->~Vec();
            a.free(s->buf);
            a.free(s);
            --*g_live;
          };
          timers.arm(*s, now + 60000 * (5 + rng.below(26)));
        }
      }
      // Traffic on a random live session: its buffer grows, or is dropped.
      if (live_sessions && rng.below(3) == 0) {
        Session* s = by_id.find(next_id - 1 - rng.below(live_sessions < 50 ? live_sessions : 50));
        if (s) {
          if (rng.below(8) == 0) {
            s->buf->clear();
          } else {
            for (uint32_t n = rng.below(64); n--;) {
              if (!s->buf->push(uint8_t(n))) break;
            }
          }
          lru.move_front(*s);
        }
      }
      // Routes: one added or dropped a second.
      if (now % 1000 == 0) {
        const uint32_t i = rng.below(256);
        if (routes_lens[i]) {
          routes.remove(routes_keys[i], routes_lens[i]);
          routes_lens[i] = 0;
        } else {
          routes_keys[i] = uint64_t(rng()) << 32 | rng();
          routes_lens[i] = uint8_t(8 + rng.below(25));
          if (!routes.insert(routes_keys[i], routes_lens[i], route_value)) routes_lens[i] = 0;
        }
      }
      timers.run(now);
      if (now % (3600 * 1000) == 0) {
        const prim::Stats f = arena.stats(Mem::Fast), b = arena.stats(Mem::Bulk);
        const Census cf = census(arena, Mem::Fast), cb = census(arena, Mem::Bulk);
        std::printf("%5llu %8" PRIu32 " %8" PRIu32 " %6" PRIu32 " %6" PRIu32 " | %8" PRIu32 " %8" PRIu32 " %6" PRIu32 " %6" PRIu32 " | %5" PRIu32 " %5" PRIu32 " %6" PRIu32 " %6" PRIu32 "\n",
                    (unsigned long long)(now / 3600000), f.free, f.largest, cf.blocks, cf.longest, b.free, b.largest,
                    cb.blocks, cb.longest, live_sessions, frames.in_use(), routes.size(), arena.failures());
        if (arena.check()) {
          std::printf("arena check failed: %d\n", arena.check());
          rc = 1;
        }
      }
    }
    std::printf("arena calls %llu, %.0f ns each; refused %" PRIu32 "; fast least free %" PRIu32 ", bulk least free %" PRIu32 "\n",
                (unsigned long long)timed.calls, double(timed.ns) / double(timed.calls), refused,
                arena.stats(Mem::Fast).least, arena.stats(Mem::Bulk).least);
    // Everything back.
    while (prim::Timer* t = timers.due(prim::TimerQueue::kNever)) {
      if (t->fire) t->fire(*t);
    }
    for (int i = 0; i < 256; ++i) {
      if (routes_lens[i]) routes.remove(routes_keys[i], routes_lens[i]);
    }
    }
    if (arena.used_bytes() || arena.check()) {
      std::printf("left allocated: %" PRIu32 " octets, check %d\n", arena.used_bytes(), arena.check());
      rc = 1;
    }
    std::printf("%s\n", rc ? "FAILED" : "passed");
  }
  std::free(fast_mem);
  std::free(bulk_mem);
  return rc;
}
