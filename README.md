# prim: storage primitives

A header-only C++20 library of an allocator and the containers built on it,
for 32-bit microcontrollers with little RAM and no heap worth fragmenting.
Written for the ESP32-S3 and built and tested for the Heltec WiFi LoRa 32 V3
and V4 and the LilyGO T-Deck; nothing in it is specific to a board. It takes
memory it is given and touches no peripheral.

- No exceptions, no RTTI, no `std::` containers, no dependencies beyond the
  C and C++ standard libraries, nothing global but the hash key.
- Every primitive that holds memory takes it from an `Alloc` it is given.
  The containers themselves allocate nothing per object: their links live in
  the objects.
- An allocated block costs one word. Memory is of two kinds, fast and bulk,
  and a request says which it wants.
- Every operation does the same work for the same inputs: nothing reads the
  clock, draws a random number or recurses. Bounds are stated with each
  primitive.

| Header | Primitive |
| --- | --- |
| `prim/alloc.h` | `Alloc`, the allocator interface; `Stats`; `Account`, a module's allocator |
| `prim/arena.h` | `Arena`, a heap over memory it is given, in two kinds |
| `prim/list.h` | `List<T, Tag>`, intrusive doubly linked list |
| `prim/hash.h` | `Hash<T, Tr, Tag>`, intrusive chained hash table |
| `prim/rbtree.h` | `Tree<T, Tr, Tag>`, intrusive red-black tree, and the node functions under it |
| `prim/timer.h` | `TimerQueue` and `Timer`, a queue of deadlines on the tree |
| `prim/trie.h` | `Trie<V>`, PATRICIA trie with longest-prefix match on 64-bit keys |
| `prim/vec.h` | `Vec<T>`, growable array of trivially copyable values |
| `prim/ring.h` | `Ring<T>`, growable ring buffer |
| `prim/pool.h` | `Pool`, fixed-size objects from slabs |
| `prim/sort.h` | `sort`, stable insertion sort |
| `prim/siphash.h` | `halfsiphash`, `hash_seed`, `hash32`: HalfSipHash-2-4 |

## Using it

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
options `PRIM_TESTS` (the suite and the example, on by default),
`PRIM_SANITIZE` (ASan and UBSan) and `PRIM_COVERAGE` (gcov, unoptimised)
apply to the tests.

### Example

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

On a board, the arena's memory comes from ESP-IDF's heap once at boot:
internal RAM from `heap_caps_malloc(MALLOC_CAP_INTERNAL)` added as
`Mem::Fast`, and PSRAM from `MALLOC_CAP_SPIRAM` as `Mem::Bulk` where the
board has it; `target/main/app.cpp` does this. Nothing in the library locks:
a program with more than one task allocating wraps every call to the arena
in a mutex or a critical section.

## Design

**Two kinds of memory.** Fast is internal RAM: what every frame or every
poll touches, and anything an interrupt or a cache-off window reads, goes
there. Bulk is whatever else there is, PSRAM on a board that has it. One
arena holds areas of both kinds and every request says which it wants. A
request for bulk is a preference: met from bulk, or from fast if no bulk
fits. A request for fast is a requirement: refused, and counted, rather than
met from bulk, so a hot structure never lands in slow memory by the luck of
the moment. On a board with only internal RAM, which adds it all as fast, the
kind is ignored in effect.

**One word per allocation.** The arena keeps a block's capacity and three
flags in a single word before it. A free block carries its list links and a
foot inside its own capacity, so an allocated block costs 4 octets on the S3.

**Intrusive containers.** The list, hash table, tree and timer queue keep
their links in the objects they hold, which derive from a hook once for each
container they can be in. Inserting cannot fail for want of memory (except
the hash table's bucket array), an object can be in several containers at
once, and finding the object from its link is a cast. A tree node is three
words, its colour in the low bit of the parent pointer; a list link is two
words, a hash link one.

**Written once.** The work that does not depend on the element type
(balancing a tree, rehashing, shifting bytes, growing a ring) is on nodes or
bytes, in a non-template core. A template adds only what does: comparing,
hashing, casting.

**Nothing held until needed.** A table, array or ring starts empty and holds
no memory; it grows when it fills, up to a maximum it is given. The hash
table keeps two objects a bucket. A pool costs nothing per object and two
words per slab. Counts are 16-bit where nothing needs more: an array holds
at most 65,535 elements, a ring 32,767, a pool slab 65,535 octets.

**Determinism.** No operation reads the clock, draws a random number (the
hash key is given to it, once) or reads memory it has not written. There is
no recursion: the trie frees its nodes over an explicit stack of 66, the
depth a 64-bit key allows. The arena's walk is over the free blocks of one
kind, a hash chain averages under two, a tree operation is O(log n), a pool's
get and put are over its slabs, the sort is for a few dozen elements.

## API

Everything is in `namespace prim`. Sizes are on the ESP32-S3 (4-octet
pointers).

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
  static uint32_t capacity_for(uint32_t n);  // what an allocation of n gets; 0 if n is too big

  void add(void* p, uint32_t n, Mem kind = Mem::Bulk);

  void* alloc(uint32_t n, Mem want = Mem::Bulk) override;
  void* resize(void* p, uint32_t n) override;
  void free(void* p) override;
  uint32_t size_of(const void* p) const override;
  Stats stats(Mem kind) const override;

  uint32_t free_bytes() const;    // over both kinds
  uint32_t used_bytes() const;
  uint32_t largest_free() const;
  uint32_t blocks() const;        // free blocks, over both kinds
  uint32_t bad_frees() const;     // frees or resizes of what was not allocated: refused and counted
  uint32_t failures() const;      // allocations refused for want of memory
  int check() const;
};
```

A heap over memory it is given, with boundary tags. `add(p, n, kind)` gives
it an area, as many as you like of either kind; each is trimmed to the
alignment at both ends and ends in a sentinel head, so nothing reads past it.
An area too small for one block and its sentinel is ignored. Areas never
merge. There is a free list for each kind, in address order, with no two
free blocks touching.

A block is one word, the head, then its capacity. The head holds the
capacity and three flags: this block allocated, the block before it
allocated, and the block in fast memory. Every capacity is the same modulo
8, so the flags share the word. A free block keeps its two list links and,
in its last word, its capacity again (the foot), so the block after it can
find its head and merge with it.

`alloc` is first fit from the head of the free list of the kind wanted; a
bulk request that finds nothing tries the fast list, a fast request never
tries bulk. A block that is freed merges with a free neighbour on either
side in O(1); only a block with both neighbours allocated walks the list to
its place. `resize` grows in place over a free block after it when that is
big enough, else allocates, copies and frees; it shrinks in place and gives
the rest back. Every block and pointer is aligned to `kAlign`.

`check()` walks each free list against itself and the counters and returns
0, or the number of the first thing wrong: 1 alignment, 2 a used or
undersized block listed free, 3 a block on the other kind's list, 4 a broken
back link, 5 disorder or a missed merge, 6 a wrong foot or neighbour's mark,
7 the free total.

Not thread safe. 48 octets, plus 4 per allocated block.

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

  void push_front(T& x);
  void push_back(T& x);
  void insert_after(T& at, T& x);
  void move_front(T& x);       // for least-recently-used order
  T* pop_front();              // nullptr when empty

  static bool linked(const T& x);
  static void remove(T& x);    // the list is not needed
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

  uint32_t size() const;
  uint32_t buckets() const;
  bool insert(T& x);           // false only when there are no buckets at all
  bool remove(const T& x);     // false if x was not in the table
  T* find(const Key& k) const;        // the most recently inserted with k, or nullptr
  T* find_next(const T& x) const;     // the one inserted before x with x's key, or nullptr
  template <class F> void each(F f) const;  // every object, in no order
};
```

`T` derives from `HashHook<Tag>`. `Tr` gives `using Key`, `static Key
key(const T&)`, `static uint32_t hash(const Key&)` and `static bool eq(const
Key&, const Key&)`.

The buckets double, from 4, when the table holds twice as many objects as
buckets, each object going to its new bucket by its own hash. If the new
array cannot be allocated the table goes on with longer chains. The buckets
never shrink; the destructor frees them. Several objects may share a key:
`insert` does not look for one already there, and `find` then `find_next`
walk them, most recent first. `each` may remove the object it is given.
Expected O(1). 16 octets plus the buckets (4 each, 2 to 4 per object), and 4
in each object.

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
  void insert(T& x);
  void remove(T& x);
  template <class K> T* lower_bound(const K& k) const;  // the first not below k, or nullptr
  template <class K> T* find(const K& k) const;
};
```

`T` derives from `RbHook<Tag>`. `Tr` gives `static int cmp(const T&, const
T&)` and, for each key type `K` looked up by, `static int cmp(const K&,
const T&)`: below 0, 0 or above. Equal keys go to the right, so equal objects
stay in the order they came. A node not in a tree is its own parent. The
node functions balance any tree whose nodes are `RbNode`s; `Tree` adds only
the comparisons, and `TimerQueue` is built on them directly. O(log n) but
`size`. 4 octets, and 12 in each object.

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
  uint32_t run(uint64_t now);        // fires every timer due, earliest first; how many
};
```

One queue of deadlines on the tree. `run` disarms then fires each timer due
by `now`, earliest first; a fire may arm or cancel any timer, and a timer
without `fire` is only disarmed. Equal deadlines come out in the order they
were armed. Deadlines are in whatever unit the caller keeps; `next()` says
how long the caller may sleep.

An owner with several timers derives from `TimerHook<Tag>` for each, and its
fire casts back through the hook:
`static_cast<Owner&>(static_cast<TimerHook<Tag>&>(t))`. No `offsetof`: an
owner need not be standard layout. O(log n). 4 octets, and 24 in each timer.

### `trie.h`: `Trie<V>`

```cpp
template <class V>
class Trie {
 public:
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
junction belongs to no object; `replace` allocates nothing. Values are
pointers the trie does not own. The destructor frees every node. A lookup
tests one bit and compares one key a level: O(key bits). 16 octets, and 24
per node.

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
  T& operator[](uint32_t i) const;

  bool push(const T& x);               // false when at max or out of memory
  bool insert(uint32_t i, const T& x); // shifting what follows
  void erase(uint32_t i);              // shifting what follows
  void erase_swap(uint32_t i);         // the last moves in
  void clear();                        // keeps the memory
  template <class Less> void sort(Less less);
  template <class K, class Less> uint32_t lower_bound(const K& k, Less less) const;
};
```

Starts empty and holds no memory; grows 4, 8, 16 and so on, by `resize`, up
to `max` (at most 65,535). `lower_bound` is the first index whose element is
not less than `k`, by `less(element, k)`, for a sorted array; `sort` is
`prim::sort`. The destructor frees the memory. `push` is O(1) amortised,
`insert` and `erase` O(n). 16 octets plus the elements.

### `ring.h`: `Ring<T>`

```cpp
template <class T>   // trivially copyable
class Ring {
 public:
  Ring(Alloc& a, uint16_t max, bool overwrite);
  uint32_t size() const;
  uint32_t capacity() const;
  bool empty() const;
  T& operator[](uint32_t i) const;   // 0 is the oldest
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

### `pool.h`: `Pool`

```cpp
class Pool {
 public:
  Pool(Alloc& a, uint32_t size, uint32_t per_slab);
  ~Pool();                  // frees every slab, whatever is still out
  void* get();              // nullptr when out of memory
  bool put(void* p);        // false, and nothing done, if p is not an object of this pool
  uint32_t in_use() const;
  uint32_t slabs() const;
  uint32_t size() const;      // an object's, as rounded
  uint32_t per_slab() const;  // as cut
};
```

Fixed-size objects from slabs. `size` is rounded up to 8 and to at least a
pointer; a slab is one allocation of a two-word header and `per_slab`
objects, the free ones threaded by 16-bit offsets, so `per_slab` is cut to
what 16-bit offsets can reach. A slab whose objects are all back goes back
to the allocator, unless it is the only one; a slab something comes back to
moves to the front, so the next `get` finds a free object at once. Putting
one object back twice is not detected. `get` and `put` are O(slabs). 12
octets plus the slabs.

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
void hash_seed(const uint8_t key[8]);           // the key hash32 uses; zero until set
uint32_t hash32(const void* in, uint32_t len);  // halfsiphash under the seeded key
```

HalfSipHash-2-4 with a 32-bit result, in 32-bit arithmetic. `hash_seed` is
called once at boot, before the first table is used, with 8 octets from the
random source (`esp_fill_random` on the S3). The key is the library's one
global: 8 octets of bss.

## Sizes

Sizes on the ESP32-S3 (4-octet pointers) of each container and of its cost
inside each object held.

| Primitive | Container | Per object |
| --- | --- | --- |
| `Arena` | 48 | 4 (a head) |
| `Account` | 24 | |
| `List` | 8 | 8 |
| `Hash` | 16 + buckets | 4 |
| `Tree` | 4 | 12 |
| `TimerQueue` | 4 | 24, with deadline and fire |
| `Trie` | 16 | node 24, allocated |
| `Vec` | 16 + elements | |
| `Ring` | 16 + elements | |
| `Pool` | 12 + slabs | 8 a slab |

Code, at `-Os` for the ESP32-S3 (ESP-IDF 5.3.2), for the parts that do not
depend on an element type: 5,510 octets in all (arena 1,646; trie 1,434;
tree 804; siphash 320; pool 295; account 247; hash 231; ring 214; timer
queue 159; array 160), no data, and 8 octets of bss (the hash key). A
template adds a few dozen octets for each type it is used with.

## Tests

    cmake -S . -B build/host -DPRIM_SANITIZE=ON && cmake --build build/host
    ./build/host/prim_tests [name-substring]
    ./coverage.sh
    target/run.sh [heltec_v3|heltec_v4|tdeck] [port]

The suite (`test/`, 55 cases) runs on the host under ASan and UBSan, and on
the ESP32-S3. Each primitive has:

- a case for each operation, its edge cases, and growth and allocation that
  fail at each step (`check::Failing` fails the n-th call, or every call
  from then on); for the arena, every way a free merges, every check, both
  kinds of memory and a board with one; for `Account`, every path of its
  counting; for the timer queue, an owner with two timers whose fires re-arm
  and cancel;
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
every figure is 100%, which it is: 762 lines and 476 branches.

The suite passes at arena alignments of 8, 16, 32 and 64
(`-DPRIM_ARENA_ALIGN`) and under any seed tried, on the host, and on the
ESP32-S3 under ESP-IDF's QEMU: the same 55 cases, compiled for Xtensa, on
ESP-IDF's C library, in a 96 KB arena. The S3 has a head layout of its own
(capacities are 4 modulo 8 there, 0 on the host, and the flag bits share the
word), which only that run exercises.

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
