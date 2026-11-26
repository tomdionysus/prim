# prim: tests

    cmake -S . -B build/host -DPRIM_BUILD_TESTING=ON -DPRIM_WARNINGS_AS_ERRORS=ON -DPRIM_SANITIZE=ON
    cmake --build build/host
    ./build/host/prim_tests [name-substring]
    ./coverage.sh
    ./build/host/prim_soak [v3|v4]
    target/run.sh [heltec_v3|heltec_v4|tdeck] [port]
    ci/host.sh       GCC and Clang, C++17 and C++20, sanitised, and the examples
    ci/arm.sh        Cortex-M0, M3, M7 and M33 under QEMU
    ci/size.sh       code size by target, into docs/sizes.md
    ci/docs.sh       the docs' code against examples/
    ci/ci.sh <step>  one step of the hosted CI: check, host, package, coverage, arm

The hosted CI (`.github/workflows/ci.yml`) runs each step of `ci/ci.sh` on
Debian 13, the host matrix on x86-64 and arm64, builds the test app for each
board and runs it as the V3 under ESP-IDF's QEMU, and builds and runs the
suite on macOS. Coverage goes to Codecov from the same run.

The suite (`test/`, 74 cases) runs on the host under ASan and UBSan, on
Cortex-M under QEMU, and on the ESP32-S3. Each primitive has:

- a case for each operation, its edge cases, and growth and allocation that
  fail at each step (`check::Failing` fails the n-th call, or every call
  from then on); for the arena, every way a free merges, every check, both
  kinds of memory, a board with one, and each way a pointer is refused; for
  `Account`, every path of its counting; for the timer queue, owners whose
  fires re-arm, cancel and run the queue;
- a case for each refusal. The tests define `PRIM_ASSERT` to count, a case
  that breaks a rule on purpose says how many reports it expects
  (`CHECK_ASSERTS`), and a case that causes any report it did not expect
  fails;
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
every figure is 100%, which it is under Debian 13's GCC 14: 921 lines and
640 branches.

The suite passes, with every warning an error, at -O0 to -O3 and -Os; at
arena alignments of 8, 16, 32 and 64 (`-DPRIM_ARENA_ALIGN`); under seven
seeds; and on the ESP32-S3 under ESP-IDF's QEMU, compiled for Xtensa, on
ESP-IDF's C library, in a 96 KB arena. The S3 has a head layout of its own
(capacities are 4 modulo 8 there, 0 on the host, and the flag bits share the
word), which only that run exercises.

`ci/arm.sh` runs the suite and the soak as bare-metal images
(`target/arm/`) on QEMU's MPS2 machines, built with the Arm GNU toolchain
and newlib, output by semihosting: for the Cortex-M3 on the AN385, the M7
on the AN500 and the M33 on the AN505, and for the M0 on the AN385's M3,
which runs ARMv6-M code unchanged. The M0 build has no divide or
count-leading-zeros instruction to use, so it proves the library does
without them: the pool divides only in its constructor. QEMU does not trap
an unaligned access as an M0 would; UBSan on the host checks alignment.

`prim_soak` (`bench/soak.cpp`) runs a simulated day of a node's memory:
frames from a pool on timers, sessions coming and going in a table, a list
and a timer each with a buffer that grows and is dropped, and a routing trie
on a pool, about 275,000 arena calls. Each simulated hour it prints, for each
kind, what is free, the largest block, the free blocks and the longest
class. It fails if a check ever fails or anything is left allocated at the
end. On the host, for a V4's shape (96 KB fast, 1.5 MB bulk, a 16 KB fast
reserve) and a V3's (192 KB, all fast), with each call's time on a Raspberry Pi 5,
the two clock reads around it included:

| Shape | Arena call | Free blocks, worst hour | Longest class | Failures |
| --- | --- | --- | --- | --- |
| V4 | 94 ns | 4 fast, 103 bulk | 39 | 0 |
| V3 | 102 ns | 117 | 42 | 0 |
