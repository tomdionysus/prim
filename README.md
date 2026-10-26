# prim: storage primitives

Version 0.1.0 (`CHANGELOG.md`).

A header-only C++20 library of an allocator and the containers built on it,
for 32-bit microcontrollers with little RAM and no heap worth fragmenting.
Written for the ESP32-S3 and built and tested for the Heltec WiFi LoRa 32 V3
and V4 and the LilyGO T-Deck; nothing in it is specific to a board. It takes
memory it is given and touches no peripheral.

- No exceptions, no RTTI, no `std::` containers, no dependencies beyond the
  C and C++ standard libraries, and 9 octets of global state: the hash key
  and whether it is set.
- Every primitive that holds memory takes it from an `Alloc` it is given.
  The containers themselves allocate nothing per object: their links live in
  the objects.
- An allocated block costs one word. Memory is of two kinds, fast and bulk,
  and a request says which it wants.
- A call that would corrupt a structure is refused: a pointer the arena did
  not give out, an object put in a container it is already in, an index
  past the end, a pool object put back twice. `PRIM_ASSERT` reports each.
- Every operation does the same work for the same inputs: nothing reads the
  clock, draws a random number or recurses. Bounds are stated with each
  primitive.

| Header | Primitive |
| --- | --- |
| `prim/alloc.h` | `Alloc`, the allocator interface; `Stats`; `Account`, a module's allocator |
| `prim/arena.h` | `Arena`, a heap over memory it is given, in two kinds |
| `prim/pool.h` | `Pool`, fixed-size objects from slabs; an `Alloc` too |
| `prim/list.h` | `List<T, Tag>`, intrusive doubly linked list |
| `prim/hash.h` | `Hash<T, Tr, Tag>`, intrusive chained hash table |
| `prim/rbtree.h` | `Tree<T, Tr, Tag>`, intrusive red-black tree, and the node functions under it |
| `prim/timer.h` | `TimerQueue` and `Timer`, a queue of deadlines on the tree |
| `prim/trie.h` | `Trie<V>`, PATRICIA trie with longest-prefix match on 64-bit keys |
| `prim/vec.h` | `Vec<T>`, growable array of trivially copyable values |
| `prim/ring.h` | `Ring<T>`, growable ring buffer |
| `prim/sort.h` | `sort`, stable insertion sort |
| `prim/siphash.h` | `halfsiphash`, `hash_seed`, `hash32`: HalfSipHash-2-4 |
| `prim/secure.h` | `secure_zero`, clearing a secret |
| `prim/assert.h` | `PRIM_ASSERT`, how misuse is reported |

## Installing

### As an ESP-IDF component

The repository root is the component, named `prim` after its directory.
Check it out under your project's `components/`, as a submodule or a copy:

    git submodule add git@github.com:tomdionysus/prim.git components/prim

or point `EXTRA_COMPONENT_DIRS` at it in the project's `CMakeLists.txt`,
before `project()`:

    set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../prim")

Then, in each component that uses it:

    idf_component_register(SRCS ... REQUIRES prim)

The component carries only headers, so the code compiles in your own
translation units at your own optimisation level. It needs ESP-IDF 5.x
(built with 5.3.2) and C++20, ESP-IDF 5's default.

### On the host

    cmake -S . -B build/host && cmake --build build/host
    ./build/host/prim_tests

Or `add_subdirectory` it and link `prim::prim`, an interface library. The
options `PRIM_TESTS` (the suite, the example and the soak, on by default),
`PRIM_SANITIZE` (ASan and UBSan) and `PRIM_COVERAGE` (gcov, unoptimised)
apply to the tests.

## Quick start

A session that is on a list of sessions, in a table by address, and has a
timer. Each container keeps its link inside the object, through a hook, so
the one object is in all three at once and none of them allocates for it.
This is `example.cpp`, which the host build compiles and runs.

```cpp
#include "prim/alloc.h"
#include "prim/arena.h"
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/siphash.h"
#include "prim/timer.h"

struct ByAddr;   // tags, so one object can be in several containers
struct Expiry;
struct Session : prim::ListHook<>, prim::HashHook<ByAddr>, prim::TimerHook<Expiry> {
  uint32_t addr;
  uint32_t packets = 0;
};

// What the table needs to know about its objects.
struct SessionByAddr {
  using Key = uint32_t;
  static Key key(const Session& s) { return s.addr; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

alignas(8) static uint8_t fast_ram[16 * 1024];

int main() {
  // One arena for the whole program, over memory it is given: internal RAM
  // as fast, PSRAM (on a board with any) as bulk.
  prim::Arena arena;
  arena.add(fast_ram, sizeof fast_ram, prim::Mem::Fast);

  // A module allocates through its Account: everything it makes is of its
  // kind of memory and counted to it.
  prim::Account sessions_mem(arena, prim::Mem::Fast);

  prim::List<Session> all;                                           // allocates nothing
  prim::Hash<Session, SessionByAddr, ByAddr> by_addr(sessions_mem);  // buckets from the account
  prim::TimerQueue timers;

  Session s;
  s.addr = 0x0A000001;
  s.fire = [](prim::Timer& t) {
    Session& self = static_cast<Session&>(static_cast<prim::TimerHook<Expiry>&>(t));
    self.packets = 0;  // expired
  };

  all.push_back(s);
  by_addr.insert(s);     // false only if there are no buckets at all
  timers.arm(s, 1000);   // deadline in the caller's units

  if (Session* found = by_addr.find(0x0A000001)) ++found->packets;
  timers.run(1000);      // fires s, which is now disarmed
  by_addr.remove(s);
  prim::List<Session>::remove(s);

  return s.packets == 0 && arena.check() == 0 && sessions_mem.objects() == 1 ? 0 : 1;
}
```

## Using it

### Memory at boot

A program has one arena, given its memory once at boot. On an ESP32-S3,
take internal RAM from ESP-IDF's heap as fast memory and, on a board with
PSRAM, PSRAM as bulk. Leave ESP-IDF what it needs for Wi-Fi, Bluetooth and
its own tasks. Seed the hash key in the same place, before any table is
used.

```cpp
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "prim/arena.h"
#include "prim/siphash.h"

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

### One account per module

Each module allocates through its own `Account`, which fixes its kind of
memory and counts what it holds. Containers take the account as their
allocator, so everything a module builds is of its kind and on its bill.

```cpp
#include "prim/alloc.h"
#include "prim/pool.h"
#include "prim/trie.h"

struct Route {
  uint32_t next_hop;
};

struct Routing {
  prim::Account mem{g_arena, prim::Mem::Fast};          // looked up on every frame: fast
  prim::Pool nodes{mem, prim::Trie<Route>::kNodeSize, 32};
  prim::Trie<Route> table{nodes};                        // its nodes from the pool
};
```

Members are built in the order they are declared and destroyed in reverse,
so the account outlives the pool and the pool outlives the trie.

### Objects and containers

The list, hash table, tree and timer queue are intrusive. An object derives
from a hook for each container it can be in, with a tag type to tell two
hooks of one kind apart. The container links the object in place and
allocates nothing for it:

```cpp
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/timer.h"

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
  prim::List<Peer> recent;                       // most recently heard first
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

### Objects made and freed all the time

Anything a node makes and drops many times a second, such as frames, comes
from a `Pool` of its size, so the churn never fragments the arena. A pool
takes whole slabs from its allocator and hands out objects from them in
O(slabs).

```cpp
#include "prim/pool.h"

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

### Timers and the main loop

Every deadline in the program goes in one `TimerQueue`. The loop runs what
is due and sleeps until the next deadline, instead of scanning tables on
every poll.

```cpp
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prim/timer.h"

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

### Watching memory

```cpp
#include "esp_log.h"

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

### Several tasks

Nothing in the library locks. A program whose tasks all allocate wraps the
arena in an `Alloc` that holds a mutex around each call, and gives that to
every account:

```cpp
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "prim/alloc.h"

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

Locked g_shared(g_arena);  // then Account(g_shared, ...) in every task
```

A container is no more thread safe than the arena: one task uses it, or
the program locks around it. An interrupt handler allocates nothing.

### Checked builds

Every call that would corrupt a structure is refused, in every build: a
pointer the arena did not give out, an object put in a container it is in,
an index past the end, a pool object put back twice. To hear about it,
compile the whole program with `-DPRIM_CHECKED`, and each refusal prints its
file, line and condition and aborts. To log or count instead, define
`PRIM_ASSERT(cond)` yourself before the first prim header, the same way in
every file of the program.

### Secrets

The arena does not clear what is freed. Clear a key, a plaintext or a
password with `prim::secure_zero(p, n)` (`prim/secure.h`) before freeing
it; the compiler cannot remove that as a dead store.

## Design

**Two kinds of memory.** Fast is internal RAM: what every frame or every
poll touches, and anything an interrupt or a cache-off window reads, goes
there. Bulk is whatever else there is, PSRAM on a board that has it. One
arena holds areas of both kinds and every request says which it wants. A
request for fast is a requirement: refused, and counted, rather than met
from bulk, so a hot structure never lands in slow memory by the luck of the
moment. A request for bulk is a preference: met from bulk, or from fast if
no bulk fits, but only while fast memory stays above a reserve the program
sets, so bulk demand a peer can drive cannot starve the hot set. On a board
with only internal RAM, which adds it all as fast, the kind is ignored in
effect.

**One word per allocation.** The arena keeps a block's capacity and three
flags in a single word before it. A free block carries its list links and a
foot inside its own capacity, so an allocated block costs 4 octets on the S3.

**Size classes.** Free blocks are kept in ten classes a kind by capacity,
each a power of two wide. A free merges with its neighbours and lists the
result in O(1); an allocation scans one class and otherwise takes the first
block of the next class up that has one.

**Intrusive containers.** The list, hash table, tree and timer queue keep
their links in the objects they hold, which derive from a hook once for each
container they can be in. Inserting cannot fail for want of memory (except
the hash table's bucket array), an object can be in several containers at
once, and finding the object from its link is a cast. A tree node is three
words, its colour in the low bit of the parent pointer; a list link is two
words, a hash link one. Each link says whether its object is in a container,
so putting an object in twice is refused rather than looping a chain.

**Pointers are checked before they are believed.** The arena takes a
pointer given to `free`, `resize` or `size_of` only if it lies in the
memory it was given, is aligned as it hands pointers out, is marked
allocated, and agrees with its neighbours: the block after it must know it
is allocated, and if the block before it is free, that block's foot must
lead back to it. A stray pointer, a double free, and most overruns of the
block before are refused and counted instead of merged into the free lists.

**Written once.** The work that does not depend on the element type
(balancing a tree, rehashing, shifting bytes, growing a ring) is on nodes or
bytes, in a non-template core. A template adds only what does: comparing,
hashing, casting.

**Nothing held until needed.** A table, array or ring starts empty and holds
no memory; it grows when it fills, up to a maximum it is given. The hash
table keeps two objects a bucket. A pool costs nothing per object and two
words and a bit an object per slab. Counts are 16-bit where nothing needs
more: an array holds at most 65,535 elements, a ring 32,767, a pool slab
65,535 octets.

**Determinism.** No operation reads the clock, draws a random number (the
hash key is given to it, once) or reads memory it has not written. There is
no recursion: the trie frees its nodes over an explicit stack of 66, the
depth a 64-bit key allows. A timer's fire cannot keep `run` from returning.
Bounds are stated with each primitive below.

## API

Everything is in `namespace prim`. Sizes are on the ESP32-S3 (4-octet
pointers), read from its compiler.

### `alloc.h`: `Mem`, `Stats`, `Alloc`, `Account`

```cpp
enum class Mem : uint8_t { Bulk, Fast };

struct Stats {
  uint32_t free;     // octets free, of this kind
  uint32_t used;     // octets allocated, of this kind
  uint32_t largest;  // the biggest allocation that would succeed now
  uint32_t least;    // the least free there has been since the memory was added
};

struct Alloc {
  virtual void* alloc(uint32_t n, Mem want = Mem::Bulk) = 0;
  virtual void* resize(void* p, uint32_t n) = 0;
  virtual void free(void* p) = 0;
  virtual uint32_t size_of(const void* p) const = 0;
  virtual Stats stats(Mem kind) const = 0;
};
```

`Alloc` is what every primitive that holds memory allocates from. `alloc`
returns nullptr when it cannot. `resize` returns nullptr when it cannot and
then leaves `p` as it was; `resize(nullptr, n)` is `alloc(n)`; a moved block
stays in its kind of memory. `free(nullptr)` does nothing. `size_of` is the
block's capacity, which may exceed what was asked, or 0 if `p` is not
allocated. Sizes are 32-bit throughout.

```cpp
class Account final : public Alloc {
 public:
  explicit Account(Alloc& to, Mem kind = Mem::Bulk);
  uint32_t bytes() const;    // octets held through this account now
  uint32_t objects() const;  // blocks held now
  uint32_t peak() const;     // the most bytes() has been
};
```

`Account` is the `Alloc` one module allocates through. It asks `to` for the
module's kind of memory (an explicit ask for `Mem::Fast` passes through) and
counts what the module holds. A moved block stays counted; a refused request,
a bad free and a null count nothing. 24 octets.

### `arena.h`: `Arena`

```cpp
class Arena final : public Alloc {
 public:
  static constexpr uint32_t kAlign;      // PRIM_ARENA_ALIGN, default 8
  static constexpr uint32_t kHeader;     // one word
  static constexpr uint32_t kMinStride;  // the smallest block, head included: 16 on the S3
  static constexpr int kBins = 10;       // size classes of free blocks, a kind
  static uint32_t capacity_for(uint32_t n);  // what an allocation of n gets; 0 if n is too big
  static int bin_of(uint32_t capacity);      // a free block's class

  void add(void* p, uint32_t n, Mem kind = Mem::Bulk);
  void reserve_fast(uint32_t n);   // fast memory a bulk request may not take
  uint32_t fast_reserve() const;

  void* alloc(uint32_t n, Mem want = Mem::Bulk) override;
  void* resize(void* p, uint32_t n) override;
  void free(void* p) override;
  uint32_t size_of(const void* p) const override;
  Stats stats(Mem kind) const override;

  uint32_t free_bytes() const;    // over both kinds
  uint32_t used_bytes() const;
  uint32_t largest_free() const;
  uint32_t blocks() const;        // blocks, allocated and free, over both kinds
  uint32_t bad_frees() const;     // frees or resizes refused: what was not allocated here
  uint32_t failures() const;      // allocations refused for want of memory
  const Free* free_list(Mem kind, int bin) const;
  int check() const;
};
```

A heap over memory it is given, with boundary tags. `add(p, n, kind)` gives
it an area, as many as you like of either kind; each is trimmed to the
alignment at both ends and ends in a sentinel head, so nothing reads past it.
An area too small for one block and its sentinel is ignored. Areas never
merge.

A block is one word, the head, then its capacity. The head holds the
capacity and three flags: this block allocated, the block before it
allocated, and the block in fast memory. Every capacity is the same modulo
8, so the flags share the word. A free block keeps its two list links and,
in its last word, its capacity again (the foot), so the block after it can
find its head and merge with it. No two free blocks touch.

Free blocks are listed by kind and by class: class b holds capacities from
2^(b+4) to 2^(b+5) - 1, class 0 everything smaller and class 9 everything
from 8 KB. `alloc` scans the class of the capacity it needs, where some
blocks may be too small, then takes the first block of the next class up
that has one, where none is. A bulk request that finds nothing tries fast
memory, if at least `fast_reserve()` would stay free; a fast request never
tries bulk. `free` merges with a free neighbour on either side and lists the
result at the front of its class, in O(1). `resize` grows in place over a
free block after it when that is big enough, else allocates, copies and
frees; it shrinks in place and gives the rest back. Every block and pointer
is aligned to `kAlign`.

`free`, `resize` and `size_of` check a pointer before acting on it: it must
lie in the memory of the kind its head says, from the first head of that
kind's lowest area to the sentinel of its highest; it must be aligned; its
head must be marked allocated and its capacity end inside that memory, at a
block marked as following an allocated one; and if its head says the block
before is free, the foot before it must lie inside that memory and name the
capacity of a free block that ends exactly here. A pointer that fails is
refused: `free` does nothing, `resize` returns nullptr, `size_of` returns 0,
and the first two count it in `bad_frees` and fire `PRIM_ASSERT`.

`check()` walks every free list against itself and the counters and
returns 0, or the number of the first thing wrong: 1 a block out of
alignment or outside its memory, 2 a used or undersized block listed free,
3 a block on the other kind's list, 4 a broken back link, 5 a free block
right after a free block, 6 a wrong foot or neighbour's mark, 7 the free
total, 8 a block in the wrong class.

Not thread safe. 140 octets, plus 4 per allocated block.

### `pool.h`: `Pool`

```cpp
class Pool final : public Alloc {
 public:
  Pool(Alloc& a, uint32_t size, uint32_t per_slab);
  ~Pool();                  // frees every slab, whatever is still out
  void* get();              // nullptr when out of memory
  bool put(void* p);        // false, and nothing done, unless p is an object of this pool that is out
  uint32_t in_use() const;
  uint32_t slabs() const;
  uint32_t size() const;      // an object's, as rounded
  uint32_t per_slab() const;  // as cut

  void* alloc(uint32_t n, Mem = Mem::Bulk) override;  // an object, for n up to size()
  void* resize(void* p, uint32_t n) override;         // p itself, for n up to size()
  void free(void* p) override;                        // put
  uint32_t size_of(const void* p) const override;     // size() for an object that is out, else 0
  Stats stats(Mem kind) const override;               // the allocator's beneath it
};
```

Fixed-size objects from slabs. `size` is rounded up to 8 and to at least a
pointer. A slab is one allocation of a two-word header, a bit for each
object, set while it is out, and `per_slab` objects, the free ones threaded
by 16-bit offsets, so `per_slab` is cut to what 16-bit offsets can reach. A
slab whose objects are all back goes back to the allocator, unless it is the
only one; a slab something comes back to moves to the front, so the next
`get` finds a free object at once. An object put back twice, or one never
got, is refused by its bit. `get` and `put` are O(slabs). 20 octets plus the
slabs.

As an `Alloc` a pool serves anything that allocates objects of one size.
A trie's nodes from a pool cost one arena block a slab rather than one a
node: `Pool nodes(arena, Trie<V>::kNodeSize, 32); Trie<V> routes(nodes);`.

### `list.h`: `List<T, Tag>`

```cpp
template <class Tag = void> struct ListHook;

template <class T, class Tag = void>
class List {
 public:
  bool empty() const;
  uint32_t size() const;       // a walk
  T* front() const;            // nullptr when empty
  T* back() const;
  T* next(const T& x) const;   // nullptr after the back
  T* prev(const T& x) const;   // nullptr before the front

  void push_front(T& x);       // refused if x is on a list
  void push_back(T& x);
  void insert_after(T& at, T& x);  // refused if at is on no list, or x is on one
  void move_front(T& x);       // from any list, or none: for least-recently-used order
  T* pop_front();              // nullptr when empty

  static bool linked(const T& x);
  static void remove(T& x);    // the list is not needed; refused if x is on no list
};
```

`T` derives from `ListHook<Tag>` once for each list it can be on. The list
is circular around its own head, so no operation tests for an end; a link
not on a list is null. A list cannot be copied. Every operation but `size`
is O(1). 8 octets, plus 8 in each object.

### `hash.h`: `Hash<T, Tr, Tag>`

```cpp
template <class Tag = void> struct HashHook;

template <class T, class Tr, class Tag = void>
class Hash {
 public:
  using Key = typename Tr::Key;
  explicit Hash(Alloc& a);
  ~Hash();                     // lets go of every object still in it

  uint32_t size() const;
  uint32_t buckets() const;
  static bool linked(const T& x);     // in a table of this tag
  bool insert(T& x);           // false if x is in a table already, or there are no buckets
  bool remove(const T& x);     // false if x is not in this table
  T* find(const Key& k) const;        // the most recently inserted with k, or nullptr
  T* find_next(const T& x) const;     // the one inserted before x with x's key, or nullptr
  template <class F> void each(F f) const;  // every object, in no order
};
```

`T` derives from `HashHook<Tag>`. `Tr` gives `using Key`, `static Key
key(const T&)`, `static uint32_t hash(const Key&)` and `static bool eq(const
Key&, const Key&)`.

A link not in a table is null; every chain ends at one constant marker, so
an object knows whether it is in a table of its tag, and a second insert is
refused. The buckets double, from 4, when the table holds twice as many
objects as buckets. If the new array cannot be had, the table goes on with
longer chains and does not ask again until its count has doubled. The
buckets never shrink. The destructor frees them and clears the link of every
object still in the table, so the objects must outlive it or be removed
first. Several objects may share a key: `insert` does not look for one
already there, and `find` then `find_next` walk them, most recent first.
`each` may remove the object it is given. Expected O(1). 20 octets plus the
buckets (4 each, 2 to 4 per object), and 4 in each object.

A key chosen by a peer is hashed with `hash32` under a key set at boot, so a
peer cannot choose keys that all land in one bucket.

### `rbtree.h`: `Tree<T, Tr, Tag>` and the node functions

```cpp
struct RbNode { uintptr_t pc; RbNode* c[2]; };   // parent | colour; children 0 (left) and 1 (right)
struct RbRoot { RbNode* root = nullptr; };

bool rb_linked(const RbNode* n);
void rb_insert(RbRoot& t, RbNode* n, RbNode* parent, int side);  // n becomes parent's child on side
void rb_erase(RbRoot& t, RbNode* n);
RbNode* rb_end(const RbRoot& t, int d);   // the first (d 0) or last (d 1), or nullptr
RbNode* rb_step(const RbNode* n, int d);  // the next (d 1) or previous (d 0), or nullptr

template <class Tag = void> struct RbHook;

template <class T, class Tr, class Tag = void>
class Tree {
 public:
  bool empty() const;
  uint32_t size() const;       // a walk: the tree keeps no count
  T* first() const;
  T* last() const;
  T* next(const T& x) const;
  T* prev(const T& x) const;
  static bool linked(const T& x);
  void insert(T& x);           // refused if x is in a tree
  void remove(T& x);           // refused if x is in none
  template <class K> T* lower_bound(const K& k) const;  // the first not below k, or nullptr
  template <class K> T* find(const K& k) const;
};
```

`T` derives from `RbHook<Tag>`. `Tr` gives `static int cmp(const T&, const
T&)` and, for each key type `K` looked up by, `static int cmp(const K&,
const T&)`: below 0, 0 or above. Equal keys go to the right, so equal objects
stay in the order they came. A node not in a tree is its own parent. The
node functions balance any tree whose nodes are `RbNode`s and check
nothing; `Tree` adds the comparisons and the refusals, and `TimerQueue` is
built on the node functions directly. O(log n) but `size`. 4 octets, and 12
in each object.

### `timer.h`: `TimerQueue`, `Timer`

```cpp
struct Timer : RbNode {
  void (*fire)(Timer&) = nullptr;
  uint64_t at = 0;
};
template <class Tag = void> struct TimerHook;

class TimerQueue {
 public:
  static constexpr uint64_t kNever = ~uint64_t(0);
  void arm(Timer& t, uint64_t at);   // re-arms a timer already armed
  void cancel(Timer& t);             // a timer not armed is left alone
  static bool armed(const Timer& t);
  uint64_t next() const;             // the earliest deadline, or kNever
  Timer* due(uint64_t now);          // the earliest timer due by now, disarmed; else nullptr
  uint32_t run(uint64_t now);        // fires every timer due by now, earliest first; how many
};
```

One queue of deadlines on the tree. `run(now)` first takes every timer due
by `now` out of the tree onto a list of its own, then disarms and fires
each, earliest first. A fire may arm or cancel any timer, including one
still waiting on that list, and may call `run`, `due` or `next`. A timer a
fire arms goes into the tree, so one armed again for `now` or earlier fires
on the next `run`, not this one: no fire can keep `run` from returning. A
timer without `fire` is only disarmed. Equal deadlines come out in the order
they were armed. Deadlines are in whatever unit the caller keeps; `next()`
says how long the caller may sleep.

An owner with several timers derives from `TimerHook<Tag>` for each, and its
fire casts back through the hook:
`static_cast<Owner&>(static_cast<TimerHook<Tag>&>(t))`. No `offsetof`: an
owner need not be standard layout. O(log n). 8 octets, and 24 in each timer.

### `trie.h`: `Trie<V>`

```cpp
template <class V>
class Trie {
 public:
  static constexpr uint32_t kNodeSize;            // what each node allocates: 24 on the S3
  explicit Trie(Alloc& a);
  bool insert(uint64_t key, uint8_t len, V& v);   // false if held already, or out of memory
  V* remove(uint64_t key, uint8_t len);           // the value it held, or nullptr
  V* replace(uint64_t key, uint8_t len, V& v);    // the value it held, now v; nullptr, nothing done, if not held
  V* exact(uint64_t key, uint8_t len) const;
  V* match(uint64_t key) const;                   // the longest prefix holding key, or nullptr
  uint32_t size() const;    // prefixes
  uint32_t nodes() const;   // prefixes and junctions
};
```

Path-compressed binary trie (PATRICIA) for longest-prefix match. A key is
up to 64 bits, held from the top: a prefix of length `len` is its top `len`
bits, the rest ignored; `len` above 64 is refused. Each node is a prefix, or
a junction where two prefixes part; junctions are made and freed as prefixes
come and go, so there are never more than twice as many nodes as prefixes.
Unlike the other containers it allocates its nodes, from `a`, since a
junction belongs to no object; a `Pool` of `kNodeSize` is the allocator to
give it. `replace` allocates nothing. Values are pointers the trie does not
own. The destructor frees every node. A lookup tests one bit and compares
one key a level: O(key bits). 16 octets, and `kNodeSize` per node.

### `vec.h`: `Vec<T>`

```cpp
template <class T>   // trivially copyable
class Vec {
 public:
  Vec(Alloc& a, uint16_t max);
  uint32_t size() const;
  uint32_t capacity() const;
  bool empty() const;
  T* begin() const;
  T* end() const;
  T& operator[](uint32_t i) const;   // unchecked, but for PRIM_ASSERT

  bool push(const T& x);               // false when at max or out of memory
  bool insert(uint32_t i, const T& x); // shifting what follows; false for i past the end
  void erase(uint32_t i);              // shifting what follows; nothing for i past the end
  void erase_swap(uint32_t i);         // the last moves in; nothing for i past the end
  void clear();                        // keeps the memory
  template <class Less> void sort(Less less);
  template <class K, class Less> uint32_t lower_bound(const K& k, Less less) const;
};
```

Starts empty and holds no memory; grows 4, 8, 16 and so on, by `resize`, up
to `max` (at most 65,535). `lower_bound` is the first index whose element is
not less than `k`, by `less(element, k)`, for a sorted array; `sort` is
`prim::sort`. An index past the end is refused by every call that changes
the array, and reported by `PRIM_ASSERT` from `operator[]`, which does not
check it otherwise. The destructor frees the memory. `push` is O(1)
amortised, `insert` and `erase` O(n). 16 octets plus the elements.

### `ring.h`: `Ring<T>`

```cpp
template <class T>   // trivially copyable
class Ring {
 public:
  Ring(Alloc& a, uint16_t max, bool overwrite);
  uint32_t size() const;
  uint32_t capacity() const;
  bool empty() const;
  T& operator[](uint32_t i) const;   // 0 is the oldest; unchecked, but for PRIM_ASSERT
  T* oldest() const;                 // nullptr when empty
  T* newest() const;

  bool push(const T& x);   // false when refused
  void pop();              // the oldest; nothing when empty
  void clear();
};
```

Oldest to newest. Grows as the array does, up to `max` (at most 32,767).
When it is full and cannot grow, it overwrites the oldest if made with
`overwrite`, else refuses the newcomer. O(1). 16 octets plus the elements.

### `sort.h`: `sort`

```cpp
template <class T, class Less>
void sort(T* begin, T* end, Less less);   // less(a, b): a goes before b
```

Insertion sort: stable, in place, nothing allocated. O(n) when the array is
nearly sorted and O(n^2) at worst, so for a few dozen elements.

### `siphash.h`: `halfsiphash`, `hash_seed`, `hash32`

```cpp
uint32_t halfsiphash(const void* in, uint32_t len, const uint8_t key[8]);
bool hash_seed(const uint8_t key[8]);           // the key hash32 uses: once only
uint32_t hash32(const void* in, uint32_t len);  // halfsiphash under the seeded key
```

HalfSipHash-2-4 with a 32-bit result, in 32-bit arithmetic; its time depends
on the length alone. `hash_seed` is called once at boot, before the first
table is used, with 8 octets from the random source (`esp_fill_random` on
the S3). The first call sets the key and returns true. Every later call
changes nothing and returns false, since a new key would strand every object
already in a table. Until it is set the key is zero, which hashes well but
predictably. The key and its flag are 9 octets of bss.

### `secure.h`: `secure_zero`

```cpp
void secure_zero(void* p, uint32_t n);
```

Clears `n` octets with volatile stores, which the compiler cannot remove as
dead. The arena does not clear what is freed: a block keeps its contents,
past the free-list links written into its first words, until it is
allocated and overwritten. A key, a plaintext or a password is cleared by
its owner before it is freed.

### `assert.h`: `PRIM_ASSERT`

`PRIM_ASSERT(cond)` is how each refusal is reported. It is nothing by
default, prints and aborts under `PRIM_CHECKED`, and is yours to define
otherwise (see "Checked builds").

## Sizes

On the ESP32-S3, read from its compiler: each container, and its cost
inside each object held.

| Primitive | Container | Per object |
| --- | --- | --- |
| `Arena` | 140 | 4 (a head) |
| `Account` | 24 | |
| `Pool` | 20 + slabs | 8 a slab, and a bit an object |
| `List` | 8 | 8 |
| `Hash` | 20 + buckets | 4 |
| `Tree` | 4 | 12 |
| `TimerQueue` | 8 | 24, with deadline and fire |
| `Trie` | 16 | node 24, allocated |
| `Vec` | 16 + elements | |
| `Ring` | 16 + elements | |

The library is headers, so a program carries the code of what it calls and
nothing else. Measured in the test app, which uses every primitive, at `-Os`
for the ESP32-S3 (ESP-IDF 5.3.2), the code that does not depend on an
element type is 7,241 octets with everything used:

| Part | Octets |
| --- | --- |
| `Arena` | 2,322 |
| `Trie` | 1,444 |
| `Pool` | 846 |
| `Tree` and the node functions | 804 |
| `TimerQueue` | 428 |
| HalfSipHash | 401 |
| `Hash` | 375 |
| `Account` | 247 |
| `Ring` | 214 |
| `Vec` | 160 |

Three things move these figures in a given program:

- An `Arena`, `Pool` or `Account` brings all five of its `Alloc` methods
  with it, called or not, since they are reached through its virtual table.
- Each type a template is used with adds a few dozen octets: a tree's
  comparisons, a table's hashing, the casts.
- A function the compiler inlines is counted in its caller, not above.

Data: 9 octets of bss (the hash key and whether it is set) and 4 of flash
(the end of every hash chain).

## Tests

    cmake -S . -B build/host -DPRIM_SANITIZE=ON && cmake --build build/host
    ./build/host/prim_tests [name-substring]
    ./coverage.sh
    ./build/host/prim_soak [v3|v4]
    target/run.sh [heltec_v3|heltec_v4|tdeck] [port]

The suite (`test/`, 71 cases) runs on the host under ASan and UBSan, and on
the ESP32-S3. Each primitive has:

- a case for each operation, its edge cases, and growth and allocation that
  fail at each step (`check::Failing` fails the n-th call, or every call
  from then on); for the arena, every way a free merges, every check, both
  kinds of memory, a board with one, and each way a pointer is refused; for
  `Account`, every path of its counting; for the timer queue, owners whose
  fires re-arm, cancel and run the queue;
- a case for each refusal. The tests define `PRIM_ASSERT` to count, a case
  that breaks a rule on purpose says how many reports it expects
  (`CHECK_ASSERTS`), and a case that causes any report it did not expect
  fails;
- a seeded random run of thousands of operations against a `std::` oracle
  (the list against `std::list`, the hash table against `std::map`, the tree
  against `std::multiset` with its red-black rules checked after every
  change, the trie against a brute-force longest match, the array, ring and
  pool against `std::vector` and `std::deque`, the sort against
  `std::stable_sort`), its own invariants checked after every operation; the
  arena's run walks every block of the area after every call. `PRIM_SEED`
  moves them;
- HalfSipHash against the reference's own vectors.

`coverage.sh` builds unoptimised with gcov, runs the suite and reports each
header's lines and branches. A template is compiled into several files; a
line counts as run, and a branch as taken, if any copy was. It fails unless
every figure is 100%, which it is: 906 lines and 630 branches.

The suite passes, with every warning an error, at -O0 to -O3 and -Os; at
arena alignments of 8, 16, 32 and 64 (`-DPRIM_ARENA_ALIGN`); under seven
seeds; and on the ESP32-S3 under ESP-IDF's QEMU, compiled for Xtensa, on
ESP-IDF's C library, in a 96 KB arena. The S3 has a head layout of its own
(capacities are 4 modulo 8 there, 0 on the host, and the flag bits share the
word), which only that run exercises.

`prim_soak` (`bench/soak.cpp`) runs a simulated day of a node's memory:
frames from a pool on timers, sessions coming and going in a table, a list
and a timer each with a buffer that grows and is dropped, and a routing trie
on a pool, about 275,000 arena calls. Each simulated hour it prints, for each
kind, what is free, the largest block, the free blocks and the longest
class. It fails if a check ever fails or anything is left allocated at the
end. On the host, for a V4's shape (96 KB fast, 1.5 MB bulk, a 16 KB fast
reserve) and a V3's (192 KB, all fast), with each call's time on a Raspberry Pi 5,
the two clock reads around it included:

| Shape | Arena call | Free blocks, worst hour | Longest class | Failures |
| --- | --- | --- | --- | --- |
| V4 | 94 ns | 4 fast, 103 bulk | 39 | 0 |
| V3 | 102 ns | 117 | 42 | 0 |

## Boards

`target/` is an ESP-IDF project that builds the suite as an application for
one of three boards, selected by an argument file that layers the board's
`sdkconfig.<board>` over `sdkconfig.defaults`:

| Board | Chip | Flash | PSRAM | Console | Build |
| --- | --- | --- | --- | --- | --- |
| Heltec WiFi LoRa 32 V3 | ESP32-S3FN8 | 8 MB | none | UART0 (CP2102) | `idf.py @heltec_v3.args build` |
| Heltec WiFi LoRa 32 V4 | ESP32-S3R2 | 16 MB | 2 MB quad | USB-serial-JTAG | `idf.py @heltec_v4.args build` |
| LilyGO T-Deck | ESP32-S3FN16R8 | 16 MB | 8 MB octal | USB-serial-JTAG | `idf.py @tdeck.args build` |

Before the suite, the application gives an arena the board's own memory,
internal RAM as fast and PSRAM as bulk, allocates from each and checks with
`esp_ptr_external_ram` that a fast request was never met from PSRAM and a
bulk request was met from it where there is any. On the V3 bulk requests
fall through to internal RAM, as `alloc.h` says they do.

`target/run.sh [board]` builds and runs under QEMU (install it with
`idf_tools.py install qemu-xtensa`); QEMU models neither PSRAM nor native USB,
so there the V3 is the one to run. `target/run.sh <board> <port>` flashes a
board instead, overwriting whatever application it holds, and reads the
result from its console. Either fails unless the suite passes.

## Licence

MIT (`LICENSE`).
