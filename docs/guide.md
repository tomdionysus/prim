# prim: using it

How a program sets prim up and uses it, from boot to the main loop. The
code is taken from `examples/`: `examples/modules.cpp`, which the host build
runs, and `examples/esp32/board.cpp`, which the ESP-IDF test app compiles
and which lists the ESP-IDF headers each part needs.

## Memory at boot

A program has one arena, given its memory once at boot. On an ESP32-S3,
take internal RAM from ESP-IDF's heap as fast memory and, on a board with
PSRAM, PSRAM as bulk. Leave ESP-IDF what it needs for Wi-Fi, Bluetooth and
its own tasks. Seed the hash key in the same place, before any table is
used.

<!-- code: examples/esp32/board.cpp 30-46 -->
```cpp
prim::Arena g_arena;

void memory_init() {
  const uint32_t fast_len = 96 * 1024;
  g_arena.add(heap_caps_malloc(fast_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), fast_len, prim::Mem::Fast);

  // On a V4 or a T-Deck; on a V3 there is none, and the call adds nothing.
  const uint32_t bulk_len = uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)) / 2;
  if (bulk_len) g_arena.add(heap_caps_malloc(bulk_len, MALLOC_CAP_SPIRAM), bulk_len, prim::Mem::Bulk);

  // Bulk requests fall back to fast memory only while this much stays free.
  g_arena.reserve_fast(16 * 1024);

  uint8_t key[8];
  esp_fill_random(key, sizeof key);
  prim::hash_seed(key);
}
```

`add` ignores a null pointer, so a failed `heap_caps_malloc` adds nothing
rather than crashing. Several areas of either kind may be added, at any
time.

## One account per module

Each module allocates through its own `Account`, which fixes its kind of
memory and counts what it holds. Containers take the account as their
allocator, so everything a module builds is of its kind and on its bill.

<!-- code: examples/modules.cpp 26-34 -->
```cpp
struct Route {
  uint32_t next_hop;
};

struct Routing {
  prim::Account mem{g_arena, prim::Mem::Fast};  // looked up on every frame: fast
  prim::Pool nodes{mem, prim::Trie<Route>::kNodeSize, 32};
  prim::Trie<Route> table{nodes};  // its nodes from the pool
};
```

Members are built in the order they are declared and destroyed in reverse,
so the account outlives the pool and the pool outlives the trie.

## Objects and containers

The list, hash table, tree and timer queue are intrusive. An object derives
from a hook for each container it can be in, with a tag type to tell two
hooks of one kind apart. The container links the object in place and
allocates nothing for it:

<!-- code: examples/modules.cpp 37-54 -->
```cpp
struct ById;
struct Expiry;
struct Peer : prim::ListHook<>, prim::HashHook<ById>, prim::TimerHook<Expiry> {
  uint32_t id;
};

struct PeerById {
  using Key = uint32_t;
  static Key key(const Peer& p) { return p.id; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

struct Peers {
  prim::Account mem{g_arena, prim::Mem::Fast};
  prim::Hash<Peer, PeerById, ById> by_id{mem};  // only its bucket array is allocated
  prim::List<Peer> recent;                      // most recently heard first
};
```

The rules that follow from this:

- A container does not own its objects. Allocate them however suits,
  from a pool, an account, or statically, and free them yourself.
- Take an object out of every container it is in before you free it.
  `linked(x)` says whether it is in one.
- An object goes into a container once. A second insert is refused.
- Destroy a container before the memory its objects are in. A hash table's
  destructor lets go of the objects still in it, so they must still exist
  then; the other containers' destructors touch nothing.
- Destroy every container before the arena or account it allocates from.

## Objects made and freed all the time

Anything a node makes and drops many times a second, such as frames, comes
from a `Pool` of its size, so the churn never fragments the arena. A pool
takes whole slabs from its allocator and hands out objects from them in
O(slabs).

<!-- code: examples/modules.cpp 57-71 -->
```cpp
struct Frame {
  uint8_t len;
  uint8_t body[255];
};

prim::Account radio_mem(g_arena, prim::Mem::Fast);
prim::Pool frames(radio_mem, sizeof(Frame), 16);

void on_receive(const uint8_t* data, uint8_t len) {
  auto* f = static_cast<Frame*>(frames.get());
  if (!f) return;  // out of memory: drop it
  f->len = len;
  for (uint8_t i = 0; i < len; ++i) f->body[i] = data[i];
  // ... queue it; later, frames.put(f)
}
```

A pool is an `Alloc` too, so anything that allocates objects of one size
can take one: a `Trie`'s nodes, as above.

## Timers and the main loop

Every deadline in the program goes in one `TimerQueue`. The loop runs what
is due and sleeps until the next deadline, instead of scanning tables on
every poll.

<!-- code: examples/esp32/board.cpp 54-75 -->
```cpp
prim::TimerQueue g_timers;

void expire(prim::Timer& t) {
  Peer& p = static_cast<Peer&>(static_cast<prim::TimerHook<Expiry>&>(t));
  // ... take p out of its containers and free it
  (void)p;
}

void heard(Peer& p, uint64_t now_ms) {
  static_cast<prim::TimerHook<Expiry>&>(p).fire = expire;
  g_timers.arm(p, now_ms + 30000);  // re-arms it if it was armed
}

void main_loop() {
  for (;;) {
    const uint64_t now = uint64_t(esp_timer_get_time()) / 1000;  // ms
    g_timers.run(now);
    const uint64_t next = g_timers.next();
    const uint64_t wait = next == prim::TimerQueue::kNever ? 1000 : next > now ? next - now : 0;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(uint32_t(wait)));  // or until another task wakes it
  }
}
```

A fire may arm, re-arm or cancel any timer. A timer armed again for now or
earlier fires on the next `run`, so a fire can never keep `run` from
returning. An object with several timers derives from a `TimerHook` for each
and casts back through the one its fire was given.

## Watching memory

<!-- code: examples/esp32/board.cpp 78-86 -->
```cpp
void memory_report() {
  const prim::Stats fast = g_arena.stats(prim::Mem::Fast);
  const prim::Stats bulk = g_arena.stats(prim::Mem::Bulk);
  ESP_LOGI("mem", "fast %u free, %u largest, %u least", unsigned(fast.free), unsigned(fast.largest),
           unsigned(fast.least));
  ESP_LOGI("mem", "bulk %u free, %u largest, %u least", unsigned(bulk.free), unsigned(bulk.largest),
           unsigned(bulk.least));
  ESP_LOGI("mem", "refused %u, bad frees %u", unsigned(g_arena.failures()), unsigned(g_arena.bad_frees()));
}
```

`least` is the low-water mark: how close each kind has come to running
out. Each module's `Account` gives its own `bytes`, `objects` and `peak`.
`g_arena.check()` walks the free lists and returns 0 when they are sound; it
is cheap enough to call from a periodic health check in a debug build.

## Several tasks

Nothing in the library locks. A program whose tasks all allocate wraps the
arena in an `Alloc` that holds a mutex around each call, and gives that to
every account:

<!-- code: examples/esp32/board.cpp 89-128 -->
```cpp
class Locked final : public prim::Alloc {
 public:
  explicit Locked(prim::Alloc& to) : to_(to), m_(xSemaphoreCreateMutexStatic(&buf_)) {}
  void* alloc(uint32_t n, prim::Mem want = prim::Mem::Bulk) override {
    Hold h(m_);
    return to_.alloc(n, want);
  }
  void* resize(void* p, uint32_t n) override {
    Hold h(m_);
    return to_.resize(p, n);
  }
  void free(void* p) override {
    Hold h(m_);
    to_.free(p);
  }
  uint32_t size_of(const void* p) const override {
    Hold h(m_);
    return to_.size_of(p);
  }
  prim::Stats stats(prim::Mem kind) const override {
    Hold h(m_);
    return to_.stats(kind);
  }

 private:
  struct Hold {
    SemaphoreHandle_t m;
    explicit Hold(SemaphoreHandle_t m) : m(m) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Hold() { xSemaphoreGive(m); }
  };
  prim::Alloc& to_;
  StaticSemaphore_t buf_;
  SemaphoreHandle_t m_;
};

// At boot, after memory_init: Locked g_shared(g_arena), then Account(g_shared, ...) in every task.
Locked* make_shared_allocator() {
  static Locked shared(g_arena);
  return &shared;
}
```

A container is no more thread safe than the arena: one task uses it, or
the program locks around it. An interrupt handler allocates nothing.

## Checked builds

Every call that would corrupt a structure is refused, in every build: a
pointer the arena did not give out, an object put in a container it is in,
an index past the end, a pool object put back twice. To hear about it,
compile the whole program with `-DPRIM_CHECKED`, and each refusal prints its
file, line and condition and aborts. To log, break into a debugger or count
instead, compile it with `-DPRIM_ASSERT_HANDLER` as well and define
`extern "C" void prim_assert_failed(const char* file, int line, const char*
cond)` once in the program. To route a refusal some other way, define
`PRIM_ASSERT(cond)` yourself before the first prim header. Whichever is
chosen, every file of the program is compiled with the same.

## Secrets

The arena does not clear what is freed. Clear a key, a plaintext or a
password with `prim::secure_zero(p, n)` (`prim/secure.h`) before freeing
it; the compiler cannot remove that as a dead store.
