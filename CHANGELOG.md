# Changelog

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
