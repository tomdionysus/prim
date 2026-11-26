# prim: storage primitives

[![Release](https://img.shields.io/github/v/tag/tomdionysus/prim?sort=semver&label=release)](https://github.com/tomdionysus/prim/tags)
[![CI](https://github.com/tomdionysus/prim/actions/workflows/ci.yml/badge.svg)](https://github.com/tomdionysus/prim/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/tomdionysus/prim/graph/badge.svg)](https://codecov.io/gh/tomdionysus/prim)
[![CodeQL](https://github.com/tomdionysus/prim/actions/workflows/codeql.yml/badge.svg)](https://github.com/tomdionysus/prim/actions/workflows/codeql.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

prim is the memory layer for firmware on a microcontroller with a few
hundred kilobytes of RAM. It gives a program one heap over memory it is
given, and the lists, tables, trees, timers and buffers it needs on top of
that heap. It is header-only C++17 with no dependencies, no exceptions, no
RTTI and no `std::` containers, and about 6 KB of code with every primitive
in use (`docs/sizes.md`).

Version 0.2.0 (`CHANGELOG.md`).

## What it is for

A node that runs for months on internal RAM, and perhaps some PSRAM, cannot
treat `malloc` as a desktop program does. prim answers the problems that
follow:

- **Fragmentation.** Objects made and dropped many times a second come from
  a `Pool` of their size; everything else comes from an `Arena` with size
  classes. A simulated day of a node's memory, about 275,000 arena calls,
  shows no growth in free blocks and no refused allocation
  (`docs/testing.md`).
- **Fast and slow memory.** On a board with PSRAM, each request says
  whether it needs internal RAM. A request for fast memory is never met from
  PSRAM, and bulk requests cannot take the last of internal RAM.
- **Knowing where memory went.** Each module allocates through its own
  `Account`, which counts what it holds and its peak. The arena reports what
  is free, its largest block, its low-water mark and every refused call.
- **Containers that do not allocate.** The list, hash table, tree and timer
  queue keep their links inside the objects. One object can be on a list, in
  a table and on a timer at once, and putting it there allocates nothing.
- **Bugs that corrupt the heap.** A double free, a stray pointer, an object
  put in a container twice and an index past the end are each refused, in
  every build, and reported through `PRIM_ASSERT`.
- **Keys a peer chooses.** Tables hash with HalfSipHash under a key set at
  boot, so a peer cannot choose keys that all land in one bucket.
- **Bounded work.** Nothing reads a clock, draws a random number or
  recurses, and each primitive states its bound (`docs/api.md`).

An allocated block costs one word; a list link two words, a tree link three,
a hash link one. The costs of each primitive are in `docs/design.md`.

## Quick start

A session that is on a list of sessions, in a table by address, and has a
timer. This is `examples/quick_start.cpp`, which the host build compiles and
runs.

<!-- code: examples/quick_start.cpp 11-68 -->
```cpp
#include "prim/alloc.h"
#include "prim/arena.h"
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/siphash.h"
#include "prim/timer.h"

// A session: on a list of sessions, in a table by peer address, with a
// timer. Each container's link is inside the object, through a hook.
struct ByAddr;  // tags, so one object can be in several containers
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
  by_addr.insert(s);    // false only if there are no buckets at all
  timers.arm(s, 1000);  // deadline in the caller's units

  if (Session* found = by_addr.find(0x0A000001)) ++found->packets;
  timers.run(1000);  // fires s, which is now disarmed
  by_addr.remove(s);
  prim::List<Session>::remove(s);

  return s.packets == 0 && arena.check() == 0 && sessions_mem.objects() == 1 ? 0 : 1;
}
```

`docs/guide.md` goes on from here: memory at boot on an ESP32-S3, one
account per module, pools, the main loop's timers, watching memory, several
tasks, checked builds and secrets.

## Where it runs

Nothing in prim is specific to a board, and it touches no peripheral. It was
written for the ESP32-S3 and is tested on these, each by a script under
`ci/`:

| Platform | Script | Last pass |
| --- | --- | --- |
| Linux arm64 (Debian 13), GCC and Clang, C++17 and C++20, ASan and UBSan | `ci/host.sh` | 2026-11-26, `5911363` |
| Cortex-M0, M3, M7 and M33, bare metal under QEMU | `ci/arm.sh` | 2026-11-25, `5911363` |
| ESP32-S3 under ESP-IDF's QEMU, as the Heltec V3 | `target/run.sh heltec_v3` | 2026-11-25, `5911363` |

The test app also builds for the Heltec WiFi LoRa 32 V4 and the LilyGO
T-Deck (`docs/boards.md`). Hosted CI runs all three on every push, the
host matrix on x86-64 as well as arm64, and builds and runs the suite on
macOS.

## Installing

### As an ESP-IDF component

The repository root is the component, named `prim` after its directory.
Check it out under your project's `components/`, as a submodule or a copy:

    git submodule add https://github.com/tomdionysus/prim.git components/prim

or point `EXTRA_COMPONENT_DIRS` at it in the project's `CMakeLists.txt`,
before `project()`:

    set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../prim")

Then, in each component that uses it:

    idf_component_register(SRCS ... REQUIRES prim)

It needs ESP-IDF 5.x (built with 5.3.2) and C++17 or later; ESP-IDF 5
compiles as C++20 by default.

### With CMake

`add_subdirectory` it and link `prim::prim`, an interface library. Added to
another project it builds and installs nothing of its own.

Or install it and find it as a package:

    cmake -S . -B build && cmake --install build --prefix /usr/local

    find_package(prim 0.2 CONFIG REQUIRED)
    target_link_libraries(app PRIVATE prim::prim)

While prim is 0.x the package accepts the same minor version only.
`prim/version.h` gives `PRIM_VERSION` and its parts.

### Build options

| Option | Default | What it does |
| --- | --- | --- |
| `PRIM_BUILD_TESTING` | `OFF` | the suite `prim_tests`, the examples and the soak `prim_soak` |
| `PRIM_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` on all of them |
| `PRIM_SANITIZE` | `OFF` | ASan and UBSan |
| `PRIM_COVERAGE` | `OFF` | gcov, unoptimised |
| `PRIM_INSTALL` | on when top level | the headers and the CMake package |

    cmake -S . -B build/host -DPRIM_BUILD_TESTING=ON -DPRIM_WARNINGS_AS_ERRORS=ON
    cmake --build build/host && ./build/host/prim_tests

## What is in it

Everything is in `namespace prim`. Each header is described in `docs/api.md`.

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
| `prim/bits.h` | `clz32`, `clz64`: leading zeros, the one thing asked of the machine |
| `prim/assert.h` | `PRIM_ASSERT`, how misuse is reported |
| `prim/version.h` | `PRIM_VERSION`, the version built against |

## Documentation

- `docs/guide.md`: setting prim up and using it in a program.
- `docs/design.md`: how it works, and what each primitive costs.
- `docs/api.md`: every header, its operations and their bounds.
- `docs/testing.md`: the suite, coverage, the soak and the CI.
- `docs/boards.md`: the ESP32-S3 test app and the boards it runs on.
- `docs/sizes.md`: code size by part, for the ESP32-S3 and each Cortex-M core.
- `CONTRIBUTING.md`: the rules for a change.

## Roadmap

- The suite and the soak on a real Heltec V3, V4 and T-Deck, for the chip's
  own timing and for PSRAM as bulk memory, which QEMU does not model.
- A locked allocator: an `Alloc` over a lock type, so a program whose tasks
  all allocate does not write its own.
- Space and behaviour changes waiting on measurements are listed in
  `TODO/ACTIVE.md`.

## Licence

MIT (`LICENSE`).
