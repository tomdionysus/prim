# prim: done

Items that were on the list and have been done, most recent first, with the
commit that did each. What remains is in `ACTIVE.md`.

## 0.2.0 (26 November 2026)

- **The 2026 group conventions (2026-11-25).** Header blocks and SPDX
  lines, `.clang-format`, `PRIM_BUILD_TESTING` and
  `PRIM_WARNINGS_AS_ERRORS`, `-Wshadow -Wconversion` on everything built,
  install and `find_package`, `prim/version.h`, the hosted CI with Codecov
  and CodeQL, the docs split into `docs/` with code from `examples/`
  checked by `ci/docs.sh`, `CONTRIBUTING.md`, and the tag `0.1.0`. History
  rewritten without `CLAUDE.md` and `basemind.toml`; the hashes in these
  files were moved to the rewritten commits. Gates: `ci/host.sh` 74 cases
  on GCC 14 and Clang, C++17 and C++20; `coverage.sh` 921 lines and 640
  branches; `ci/arm.sh` M0, M3, M7, M33; `ci/ci.sh check` and `package`.

## 0.1.0 (26 October 2026)

- **The suite under QEMU and the test app on every board.** The suite
  passes under ESP-IDF's QEMU as the V3, and the test app builds for the
  Heltec V3 and V4 and the T-Deck, in CI (`39f55ae`). Running it on the real
  boards is still item 1 of `ACTIVE.md`.
- **A fragmentation run shaped like an application.** `bench/soak.cpp`
  (`9146486`): frames from a pool, sessions coming and going in a table and
  a list with a timer each, buffers growing and being dropped, and a routing
  trie whose nodes come from a pool, over a simulated day, as the V3 (all
  fast) or the V4 (fast and bulk). Every simulated hour it logs, per kind,
  what is free, the largest block, the free-block count and the longest
  class; it fails if the arena's structures are ever wrong or anything is
  left allocated. It showed no growth in free blocks over the day, which
  settled that first fit within a class is enough for now.
- **Timings, on the host.** The soak prints what each of the arena's calls
  costs at the end of its run. The same figures on the S3, in internal RAM
  and PSRAM, are item 2 of `ACTIVE.md`.
- **Free blocks in size classes.** The arena was first fit over one
  address-ordered list a kind, so an allocation walked the free blocks and a
  free between two allocated neighbours walked to its place. It now keeps
  free blocks in ten size classes a kind (`75f7a8c`): a free is O(1) and an
  allocation looks at one class, then takes the first block of the next
  class that has one. The arena grew from 48 to 140 octets for it.
- **Double frees and double puts refused.** A pool did not detect an object
  put back twice, and the arena detected a double free only while the
  block's head was where it had been. Now a bit an object marks it free in
  the pool, and the arena checks every pointer it is given back against the
  memory it was given and the pointer's neighbours before `free`, `resize`
  or `size_of` acts on it (`75f7a8c`). With it, in the same commit: `List`,
  `Hash`, `Tree`, `Vec` and `Ring` refuse what would corrupt them,
  `TimerQueue::run` always returns whatever its fires do, `hash_seed`
  refuses a second call, and `PRIM_ASSERT` reports every refusal.
