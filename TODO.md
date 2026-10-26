# prim: to do

In order. The API is settled; nothing below changes it.

1. **The suite on the boards.** It passes under ESP-IDF's QEMU as the V3;
   `target/run.sh <board> <port>` on each of the three boards is still to
   run, for the real chip's timing and caches, and for PSRAM as bulk memory
   on the V4 and the T-Deck, which QEMU does not model.
2. **A locked allocator.** An `Alloc` adaptor that takes a FreeRTOS mutex or
   a critical section around each call to the arena, so each application
   does not write its own.
3. **A fragmentation run** shaped like an application: frames from pools,
   tables growing and shrinking, sessions coming and going, the largest free
   block of each kind logged over a simulated day. It decides each pool's
   slab size, the split between fast and bulk on the V4 and T-Deck, and
   whether first fit over one list is enough.
4. **Timings on the S3**: insert, find and remove for each container at
   realistic sizes, in fast and in bulk memory.

## Space, if measurements ask for it

- A `Timer` is 24 octets on the S3: 12 of node, 4 of `fire` and 8 of
  deadline. A 32-bit deadline relative to the queue would make it 20, at the
  cost of a wrap every 49 days to handle.
- A trie node keeps a value pointer and a length beside its key and
  children: 24 octets. Capping keys at 63 bits would let the length ride in
  the low bits of the two child pointers, for 20, at a mask on every step of
  a lookup.
- The tree's comparison could be one function pointer in a non-template
  tree, saving the template's few dozen octets per element type, at a call
  per comparison.
- The arena is 48 octets: two of everything for the two kinds of memory,
  and the counters. A board with one kind could drop a list, a few counters
  and the kind tests for about 200 octets of code.
- An `Account` is 24 octets a module: a module that never needs its figures
  could use the arena directly, through a `Prefer` of 12 that only names
  the kind.

## Behaviour

- The arena is first fit over one address-ordered list a kind: allocation
  walks the free blocks, and a free with both neighbours allocated walks to
  its place. If the fragmentation run shows either matters, segregated free
  lists by size class are the next step, at a few hundred octets of code.
- The hash table never shrinks its buckets. A `shrink()` when it falls below
  a quarter full may be wanted for tables that spike.
- A pool does not detect an object put back twice, and the arena detects a
  double free only while the block's head is where it was; a debug build
  could mark free objects.
- `prim::sort` is an insertion sort: right for a few dozen elements, wrong
  for hundreds. A heap sort would be the step up, in place and
  deterministic, at about 200 octets.
