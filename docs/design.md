# prim: design

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
Bounds are stated with each primitive in `api.md`.

## Sizes

On the ESP32-S3, read from its compiler: each container, and its cost
inside each object held.

| Primitive | Container | Per object |
| --- | --- | --- |
| `Arena` | 140 | 4 (a head) |
| `Account` | 24 | |
| `Pool` | 24 + slabs | 8 a slab, and a bit an object |
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
element type is 6,244 octets with everything used. `ci/size.sh` measures
it, and the same for each Cortex-M core, into `docs/sizes.md`:

| Part | Octets |
| --- | --- |
| `Arena` | 1,887 |
| `Trie` | 886 |
| `Pool` | 869 |
| `Tree` and the node functions | 771 |
| `TimerQueue` | 422 |
| HalfSipHash | 365 |
| `Hash` | 359 |
| `Account` | 240 |
| `Ring` | 255 |
| `Vec` | 190 |

Three things move these figures in a given program:

- An `Arena`, `Pool` or `Account` brings all five of its `Alloc` methods
  with it, called or not, since they are reached through its virtual table.
- Each type a template is used with adds a few dozen octets: a tree's
  comparisons, a table's hashing, the casts.
- A function the compiler inlines is counted in its caller, not above.

Data: 9 octets of bss (the hash key and whether it is set) and 4 of flash
(the end of every hash chain).
