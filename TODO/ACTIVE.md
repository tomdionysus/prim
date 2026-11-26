# prim: to do

What has been done is in `COMPLETED.md`. The plan for taking the library
beyond the ESP32 is `2026-10-26-generalising-plan.md`, drawn from
`2026-10-26-generalising-note.md`.

## Resume here

The repository was brought to the 2026 group conventions on 2026-11-25, on
`develop`: header blocks, `.clang-format`, the CMake options, install and
package, `prim/version.h`, the hosted CI with Codecov and CodeQL, the docs
split into `docs/` with their code from `examples/`, and `CONTRIBUTING.md`.
History was rewritten to drop `CLAUDE.md` and `basemind.toml`, and the
release tag is `0.1.0`. Next: push `main`, `develop` and the tag, then fill
the README's table of runs from the first passing runs.

## Decisions

- Licence: MIT, not the stack's GPLv3. A GPLv3 header-only library would
  bind every firmware built with it. Each file carries an SPDX line.
- Dialect: the code's own. Google style at 120 columns, 2-space indent,
  `.h` headers, `member_` and `kName`. `.clang-format` reproduces it.
- C++17, no exceptions, no RTTI, no `std::` containers: the targets are
  microcontrollers. The group's C++20, error and logging rules do not apply.
- Tests use prim's own harness (`test/check.h`), not GoogleTest: the suite
  runs bare-metal on a Cortex-M0 and on the ESP32-S3.
- Hosted CI is back, for the badges and coverage; `ci/host.sh`,
  `coverage.sh` and `ci/arm.sh` stay the local gates.

## Waits on Tom

- Push `main` (rewritten: a force push), `develop` and the tag `0.1.0`, and
  delete `v0.1.0` on origin.

## In order

1. **The suite on the boards.** It passes under ESP-IDF's QEMU as the V3,
   and the test app builds for all three boards. `target/run.sh <board>
   <port>` on a real V3, V4 and T-Deck is still to run: for the chip's own
   timing and caches, and for PSRAM as bulk memory on the V4 and the T-Deck,
   which QEMU does not model.
2. **The soak on a board.** `bench/soak.cpp` as a target app, so the
   per-call cost and the free-block counts are the S3's, in internal RAM and
   in PSRAM, rather than the host's.
3. **A locked allocator.** An `Alloc` adaptor over a lock type with
   `lock()` and `unlock()`, so a program with several tasks allocating does
   not write its own; on the S3 a FreeRTOS mutex or a critical section.
4. **The API reference from the headers.** `docs/api.md`'s declaration
   blocks are summaries typed from the headers, not taken from them; the
   rule that docs code comes from files does not yet hold there.

## Space, if measurements ask for it

- The arena is 140 octets: twenty class heads, the bounds of each kind's
  memory, and the counters. A board with one kind could drop half the heads
  and bounds; fewer, wider classes would trade a longer scan for less.
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
- An `Account` is 24 octets a module: a module that never needs its figures
  could use the arena directly, through a `Prefer` of 12 that only names
  the kind.

## Behaviour

- The arena bounds a pointer by the span of each kind's memory, from its
  lowest area to its highest, not by each area. A pointer into a gap between
  two areas of one kind is read before it is refused; on the S3 the areas of
  one kind are in one RAM, where such a read is harmless.
- Free blocks are first fit within a class and taken from the front of the
  next class up, last freed first. The soak shows no growth in free blocks
  over a day; address order within a class is the next step if a workload
  shows fragmentation.
- `Pool::put` and `Pool::size_of` find an object's slab by walking the
  slabs. A pool with many slabs would want the slab found from the object's
  address, by aligning slabs.
- The hash table never shrinks its buckets. A `shrink()` when it falls below
  a quarter full may be wanted for tables that spike.
- `prim::sort` is an insertion sort: right for a few dozen elements, wrong
  for hundreds. A heap sort would be the step up, in place and
  deterministic, at about 200 octets.
