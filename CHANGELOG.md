# Changelog

## Unreleased

## 0.2.0

- C++17 is the floor: `cxx_std_17`, and the host build defaults to it. The
  ESP-IDF test app stays at the IDF default.
- `bits.h`: `clz32` and `clz64`, the builtin under GCC and Clang, the IAR
  intrinsic, or a portable version the tests check against the builtin.
  `arena.h` and `trie.h` use them; no compiler-specific code remains
  elsewhere.
- `PRIM_ASSERT_HANDLER`: with `PRIM_CHECKED`, a refusal calls
  `prim_assert_failed(file, line, cond)`, which the program defines once, to
  log, break or count instead of printing and aborting.
- `ci/host.sh`: the suite, example and soak under GCC and Clang at C++17 and
  C++20, sanitised, and the checked build's hook.
- `CONTRIBUTING.md`: the rules for a change. `CLAUDE.md` and
  `basemind.toml` are no longer in the repository or its history.
- Every source opens with a header block and an SPDX line; `.clang-format`
  states the existing style, and the tree is formatted with it.
- CMake: `PRIM_TESTS` is now `PRIM_BUILD_TESTING`, off by default, and
  `PRIM_WARNINGS_AS_ERRORS` replaces the fixed `-Werror`. The tests,
  examples and soak build with `-Wshadow` and `-Wconversion` as well. CMake
  3.24 or later on the host.
- `prim/version.h`: `PRIM_VERSION` and its parts. CMake refuses to configure
  if it differs from `project()`.
- Install and package: `find_package(prim 0.2 CONFIG)` gives `prim::prim`,
  accepting the same minor version. Added with `add_subdirectory`, prim
  builds and installs nothing of its own.
- Hosted CI again (`ci/ci.sh`, `.github/`): the header, format and docs
  check, the host matrix on x86-64 and arm64, the installed package, coverage
  to Codecov, Cortex-M under QEMU, the board builds, the ESP32-S3 under QEMU,
  macOS, and CodeQL.
- Tests: a pointer at or past the top of an arena's memory is refused. Under
  Debian 13's GCC 14 that branch of the bounds check was never taken, so
  coverage stood at 639 of 640 branches.
- Docs: the README says what prim is for; the guide, design, API, tests and
  boards are in `docs/`. Their code is taken from `examples/` by line range,
  and `ci/docs.sh` checks it. The ESP-IDF examples compile in the test app.
- The release tag is `0.1.0`, not `v0.1.0`.
- `sort` of fewer than two elements returns at once: it computed one past the
  end of an empty range, undefined, and of an empty `Vec`, a null pointer.
  Found by Clang's UBSan.
- Tests: three hash tests declared their objects after the table, so the
  table's destructor touched them after they died. Found by Clang's ASan.
- `Pool` divides only in its constructor. An object's index is its offset
  shifted by the size's power of two and multiplied by the inverse of its
  odd part, modulo 2^32: one multiply, where a core without a divide
  instruction made a library call. A `Pool` is 24 octets on a 32-bit core,
  4 more. Its random test runs six sizes, odd parts 1 to 125.
- `target/arm/` and `ci/arm.sh`: the suite and the soak as bare-metal images
  for the Cortex-M0, M3, M7 and M33 under QEMU, all passing.
- `ci/size.sh` and `docs/sizes.md`: code size by part for the ESP32-S3 and
  each Cortex-M core. The README's figures were measured at ESP-IDF's
  debug optimisation, not `-Os` as it said: at `-Os` the S3's is 6,244
  octets, not 7,241.
- The soak prints its 32-bit figures with `PRIu32`, so it builds where
  `uint32_t` is `unsigned long`.
- The test harness fails at once if its heap cannot be had, where it had
  handed the arena memory at address 64.

## 0.1.0

The first release.

- `Arena`: a heap over memory it is given, in two kinds, fast and bulk, one
  word per allocation, with free blocks in size classes. It checks every
  pointer it is given back, keeps a fast-memory reserve from bulk requests,
  and reports its state through `stats` and `check`.
- `Account`: a module's allocator, which fixes its kind of memory and counts
  what it holds.
- `Pool`: fixed-size objects from slabs, refusing an object put back twice;
  an `Alloc` too.
- Intrusive containers: `List`, `Hash`, `Tree` and `TimerQueue`, each
  refusing an object put in twice. `TimerQueue::run` always returns,
  whatever its timers' fires do.
- `Trie`: longest-prefix match on keys of up to 64 bits.
- `Vec` and `Ring`: growable arrays and ring buffers of trivially copyable
  values, refusing indexes past the end.
- `sort`, HalfSipHash with a key seeded once, `secure_zero`, and
  `PRIM_ASSERT` for reporting misuse.
- An ESP-IDF component and a CMake interface library, with a test suite at
  full line and branch coverage, a simulated day's soak, and a test app for
  the Heltec WiFi LoRa 32 V3 and V4 and the LilyGO T-Deck.
