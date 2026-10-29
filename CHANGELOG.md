# Changelog

## Unreleased

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
- `CLAUDE.md`: the working rules.
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
