# prim: API

Everything is in `namespace prim`. Sizes are on the ESP32-S3 (4-octet
pointers), read from its compiler.

## `alloc.h`: `Mem`, `Stats`, `Alloc`, `Account`

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

## `arena.h`: `Arena`

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

## `pool.h`: `Pool`

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

## `list.h`: `List<T, Tag>`

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

## `hash.h`: `Hash<T, Tr, Tag>`

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

## `rbtree.h`: `Tree<T, Tr, Tag>` and the node functions

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

## `timer.h`: `TimerQueue`, `Timer`

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

## `trie.h`: `Trie<V>`

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

## `vec.h`: `Vec<T>`

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

## `ring.h`: `Ring<T>`

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

## `sort.h`: `sort`

```cpp
template <class T, class Less>
void sort(T* begin, T* end, Less less);   // less(a, b): a goes before b
```

Insertion sort: stable, in place, nothing allocated. O(n) when the array is
nearly sorted and O(n^2) at worst, so for a few dozen elements.

## `siphash.h`: `halfsiphash`, `hash_seed`, `hash32`

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

## `secure.h`: `secure_zero`

```cpp
void secure_zero(void* p, uint32_t n);
```

Clears `n` octets with volatile stores, which the compiler cannot remove as
dead. The arena does not clear what is freed: a block keeps its contents,
past the free-list links written into its first words, until it is
allocated and overwritten. A key, a plaintext or a password is cleared by
its owner before it is freed.

## `bits.h`: `clz32`, `clz64`

Leading zeros of a nonzero word: the builtin under GCC and Clang, the
intrinsic under IAR, else a portable version, `clz32_portable` and
`clz64_portable`, which the tests check against the builtin at every bit
position. The arena finds a size class with it and the trie the first bit
two keys differ in. Nothing else in the library asks anything of the
compiler.

## `assert.h`: `PRIM_ASSERT`

`PRIM_ASSERT(cond)` is how each refusal is reported. It is nothing by
default, prints and aborts under `PRIM_CHECKED`, calls the program's
`prim_assert_failed` under `PRIM_CHECKED` with `PRIM_ASSERT_HANDLER`, and is
yours to define outright (see "Checked builds" in `guide.md`).
