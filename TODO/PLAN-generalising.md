# prim: plan for generalising beyond the ESP32

26 October 2026. A working plan drawn from `prim-generalising-beyond-esp32.md`
(the note), checked against the repository at `ea1ed4a`. The note is the
argument; this is the order of work, what each step must show, and where the
note needed correcting. Items here that are also in `ACTIVE.md` are marked.

## What the note gets right, checked

- `include/prim/` names nothing of the ESP32 and includes no vendor, RTOS or
  SDK header. ESP-IDF appears only under `target/` and in the README.
- The only compiler-specific code is `__builtin_clz` (`arena.h:60`) and
  `__builtin_clzll` (`trie.h:45`). Both call sites already exclude zero.
- `Pool` divides by `size_` at runtime in three places (`pool.h:101`, `:102`,
  `:129`): a library call on a core without a divide instruction.
- The only C++17-or-later features are the two
  `std::is_trivially_copyable_v` asserts (`vec.h:35`, `ring.h:42`). C++20 is
  asked for only by `CMakeLists.txt` and `target/CMakeLists.txt`.
- The headers are 1,853 lines; the suite is 71 cases; `TimerQueue` deadlines
  are `uint64_t`.
- Every API name the note's how-tos lean on exists: `move_front`,
  `reserve_fast`, `bad_frees`, `failures`, `linked`, `bytes`, `objects`,
  `peak`.

## Where the note needs correcting

1. **The assert hook is smaller than described.** `PRIM_ASSERT` is already a
   no-op by default, prints and aborts only under `PRIM_CHECKED`, and can be
   defined by the user before the first header. Bare metal is not broken
   today. The change worth making is a `prim_assert_failed(file, line, cond)`
   that a checked build calls when the program asks, so it can be routed to
   RTT or a breakpoint without redefining the macro. One hour, low priority.
2. **The test suite is not bare-metal ready.** It checks every container
   against a `std::` oracle (`map`, `set`, `list`, `deque`, `vector`) over
   thousands of random operations and prints with `printf`. An Arm image
   therefore needs newlib with a heap of a few hundred kilobytes and
   semihosting for output. The S3 test app already runs it, so a few hundred
   kilobytes is enough; QEMU's `microbit` (nRF51, 16 KB) is not.
3. **A Cortex-M0 run needs a different machine.** Compile with
   `-mcpu=cortex-m0` and run the image on `mps2-an385`: an M3 executes
   ARMv6-M code unchanged, so the no-divide, no-CLZ code generation is
   proved. What it does not prove is the M0's trap on unaligned access, as
   the M3 permits it. Cover that on the host with UBSan's alignment check,
   which the sanitised build already includes, and say so in the testing
   doc.
4. **Divide-free `Pool` is not free on the M0 either.** The M0 has no 64-bit
   multiply, so Lemire's fastmod becomes a `__aeabi_lmul` call in place of
   `__aeabi_uidiv`. Cheaper, not free. Do the power-of-two shift path, which
   is cheap and covers the common sizes, and measure before doing the rest.
5. **The CI matrix assumes GitHub Actions.** The last commit removed
   `.github/workflows/ci.yml` and the README's CI lines. The plan below puts
   every job in a script under `ci/` that runs locally with the toolchains
   installed, so the matrix exists and is run before each release. Hosted CI is not
   coming back; see the decisions below.
6. **The outreach plan the note calls "the sibling document" is not in the
   repository**, and is dropped: the work ends with the repository itself.
7. **Item 3 of `TODO.md`** in the note is item 3 of `ACTIVE.md`: the locked
   allocator.

## Toolchains on this machine

| Tool | State | To install |
| --- | --- | --- |
| `g++`, `cmake`, `qemu-system-arm`, `qemu-system-xtensa` | present | |
| `arm-none-eabi-g++` 14.2 with newlib and libstdc++ | Arm GNU toolchain 14.2.rel1 under `~/.local/opt`, linked into `~/.local/bin` | Debian's packages do not install together: its Arm libstdc++ pins GCC 14.2 while its GCC candidate is 15.3 |
| `clang++` | missing | `sudo apt install clang` (needs a password, so by hand) |
| ESP-IDF 5.3.2 and its QEMU (`esp_develop_9.0.0`) | present under `~/esp/esp-idf` and `~/.espressif` | `. ~/esp/esp-idf/export.sh` in a shell that needs `idf.py` |
| PlatformIO, west | missing | only when phases 4 and 5 reach them |

The system `qemu-system-xtensa` does not model the `esp32s3` machine;
`target/run.sh` wants ESP-IDF's own, as now.

## Decisions

- **No hosted CI.** Decided 26 October 2026. Every job is a script under `ci/`
  run locally before a release; the README's supported-targets table carries
  the date and commit of each run, for QEMU and real boards alike. No badges.
- **The language floor.** C++17, as the note argues. Hold it with a job, not
  a promise.
- **Where vendor code lives.** `port/<platform>/` beside `include/`, never
  inside `include/prim/`. The FreeRTOS lock and heap are the first tenants.
- **Version.** `0.2.0` after phase 3. `1.0` waits for a user outside the
  repository, so `0.x` through phase 5.

## Phase 1: foundations

Make the library's portability a tested fact rather than a reading of the
source. No API change.

| Step | Work | Shows |
| --- | --- | --- |
| 1.1 | `cxx_std_17` in `CMakeLists.txt`; default `CMAKE_CXX_STANDARD` 17 when top level; `target/` stays `gnu++20` | 17 is the floor |
| 1.2 | `include/prim/bits.h`: `clz32`, `clz64` via the builtins under GCC and Clang, `__CLZ` where CMSIS or IAR defines it, de Bruijn fallback; `arena.h` and `trie.h` use them; keep the nonzero contract in a comment | No compiler-specific code outside one header |
| 1.3 | `PRIM_ASSERT_HANDLER`: with `PRIM_CHECKED`, call a `prim_assert_failed` the program defines (correction 1). Not a weak default in the header: that collides with the program's definition in any file that includes a prim header | A checked build can be routed without redefining the macro |
| 1.4 | `ci/host.sh`: GCC and Clang, each at 17 and 20, `-fno-exceptions -fno-rtti`, sanitised, plus the checked build's hook with its default and with a program's own; `./coverage.sh` stays as the coverage gate | Two compilers, two standards, the hook, 100% |
| 1.5 | Commit the repository `CLAUDE.md` from the note, with its test commands pointing at `ci/` | The rules are in the repo, not in prompts |

*Done when:* `ci/host.sh` and `./coverage.sh` pass with Clang installed,
and the fallback `clz` is covered by a test that forces it.

## Phase 2: Arm under QEMU

| Step | Work | Shows |
| --- | --- | --- |
| 2.1 | `target/arm/`: a bare-metal image of `test/` for `mps2-an385` with newlib-nano, a linker script giving newlib a heap of at least 256 KB, semihosting `printf` and exit; `prim_run` unchanged | The 71 cases run on Arm |
| 2.2 | `ci/arm.sh <cpu>` building and running that image for `cortex-m0` (correction 3), `cortex-m3`, `cortex-m7` on `mps2-an500`, `cortex-m33` on `mps2-an505` | Four cores pass |
| 2.3 | `Pool`: a shift when `size_` is a power of two; measure `put` on the M0 build before deciding on the rest (correction 4); add sizes 8, 24, 40, 256 to the randomised run | Common sizes divide-free; the rest decided by a number |
| 2.4 | `ci/size.sh`: `arm-none-eabi-size` per core and Xtensa for the S3, written to `docs/sizes.md` | Code size per target, generated not typed |
| 2.5 | Run `bench/soak.cpp` on `mps2-an385` with two areas standing in for TCM and SDRAM | The soak off the host, which also prepares `ACTIVE.md` item 2 |

*Done when:* all four cores print the suite's final line with zero
failures, and `docs/sizes.md` exists.

Pin QEMU and toolchain versions in the scripts. The 192 KB and 1.5 MB the
soak asks for fit `mps2-an385`'s 4 MB; the suite's needs are set by its
oracles, so measure the heap high-water mark once and write it in the
linker script with margin.

## Phase 3: FreeRTOS

| Step | Work | Shows |
| --- | --- | --- |
| 3.1 | `include/prim/locked.h`: `Locked<Lock>`, an `Alloc` over any `Lock` with `lock()` and `unlock()`, replacing the README's hand-written class (`ACTIVE.md` item 3) | One adaptor, tested on the host with a counting lock |
| 3.2 | `port/freertos/lock.h`: `FreeRtosMutex` (static mutex, task context) and `FreeRtosSuspend` (`vTaskSuspendAll`, usable before the scheduler); interrupts allocate nothing, said in the header | The S3 and vanilla FreeRTOS share one adaptor |
| 3.3 | `port/freertos/heap_prim.cpp`: `pvPortMalloc`, `vPortFree`, `xPortGetFreeHeapSize`, `xPortGetMinimumEverFreeHeapSize`, `vPortGetHeapStats`, `vPortInitialiseBlocks`; `prim_heap_add` in place of `vPortDefineHeapRegions`; `prim_heap()` for application accounts; kernel requests `Mem::Fast`; malloc-failed hook honoured | The kernel itself allocates from an arena |
| 3.4 | `examples/freertos-qemu-heap/` from the note's brief E4, on `mps2-an385`, pinned kernel tag; `ci/freertos.sh` runs it | PASS, and a stray free counted not crashed |
| 3.5 | `CHANGELOG.md`, tag `v0.2.0` | Shippable |

*Done when:* E4 prints PASS under QEMU, the `vPortFree` of a stack address
increments `bad_frees`, and the host suite covers `Locked`.

Not for ESP-IDF: it routes `pvPortMalloc` to `heap_caps`. Say so in the
file's head so nobody links it into an IDF project.

## Phase 4: the front door

| Step | Work |
| --- | --- |
| 4.1 | Move today's Design, API and Tests sections to `docs/design.md`, `docs/api.md`, `docs/testing.md` unchanged; `docs/sizes.md` from 2.4 |
| 4.2 | New README in the note's layout: why, who it is for and not for, the existing example, install per ecosystem, memory at boot per platform, which primitive, how-tos, compared with, supported targets |
| 4.3 | Check the ETL and Boost.Intrusive cells of the comparison against their current releases before publishing; the note says they are from memory |
| 4.4 | `idf_component.yml` and `library.json`; `install()` and `prim-config.cmake`; `zephyr/module.yml` |
| 4.5 | Publish to the ESP Component Registry and PlatformIO from the `v0.2.0` tag; new GitHub description and topics |
| 4.6 | Hand the README alone to a fresh agent and have it install from each registry; fix what it trips on |

*Done when:* 4.6 succeeds for ESP-IDF and PlatformIO without reading
anything but the README.

Arduino Library Manager stays out: it forces a `src/` layout.

## Phase 5: examples and how-tos

Order: E1 (move `example.cpp`), E2, E3, E7, E8 (all run or build under
`ci/`), then E5 and E6 (build; board run by hand), then the how-tos in the
note's table order. Each example is one brief from the note, one branch, one
review. Each how-to takes its code from an example by file and line range
through a snippet marker, and `ci/docs.sh` fails on a broken range.

*Done when:* every how-to links to an example `ci/` builds, and
`ci/docs.sh` passes.

## Deferred, and why

- **Timer tick type as a parameter** (note's change 7). Only if an M0
  measurement shows the 64-bit compare matters. The 32-bit variant needs a
  wrap the caller handles; not worth the doc until someone asks.
- **Full fastmod in `Pool`.** After the measurement in 2.3.
- **Real boards.** `ACTIVE.md` items 1 and 2 stand: the three S3 boards, and
  one STM32 Nucleo and one RP2040 or RP2350 when to hand. Recorded in the
  README's table with date and commit.
- **Outreach.** Dropped, 26 October 2026.

## Estimate

| Phase | Days | Mostly |
| --- | --- | --- |
| 1 Foundations | 1 | Build and one header |
| 2 Arm | 2 to 3 | Linker script, semihosting, four runs |
| 3 FreeRTOS | 2 | `heap_prim.cpp` and E4 |
| 4 Front door | 2 | Writing and registries |
| 5 Examples and how-tos | 3 | Eight projects, twelve pages |

Ten to eleven working days, as the note says, if the toolchains install
cleanly. The first day is lost to installing them if they do not.

## Open questions

- Which STM32 and RP2350 boards are to hand for E5 and E6?
- Is the comparison table's claim that ETL has no intrusive hash table or
  tree still true of the current ETL?

## Progress

Dated, most recent first. A phase is done when its done-when line has been
run and is recorded here.

- 27 October 2026, on `ea1ed4a` plus the uncommitted phase 1 and 2 changes:
  **phase 2 done.** `ci/arm.sh` passes: 73 cases and the soak as the V3 and
  the V4 on the Cortex-M0, M3, M7 and M33 under QEMU 11.1, built with the Arm
  GNU toolchain 14.2.rel1. `docs/sizes.md` is generated. `ci/host.sh`,
  `./coverage.sh` and `target/run.sh heltec_v3` pass on the same state.
  Where the work departed from the steps above:
  - 2.1: full newlib with librdimon for semihosting, not newlib-nano; the
    heap is newlib's, from the end of `.bss` to the stack, 4 MB of RAM in
    all. The start code passes QEMU's `-append` to `main`, which the soak
    uses to pick a board.
  - 2.3: not a shift for powers of two and a measurement for the rest. An
    index in a slab is an exact multiple of the size, so every size takes a
    shift and one 32-bit multiply by the inverse of its odd part, which an
    M0 does in one instruction. Correction 4's fastmod is not needed, and no
    measurement decides anything: QEMU does not count cycles in any case.
    The pool grew by 4 octets for the inverse and shift.
  - 2.4: sizes by symbol, part by part, not `size` on whole images, so they
    compare with the README's table. That showed the README's figures were
    from ESP-IDF's debug build; they are now the `-Os` ones.
  - 2.5: on all four cores, both boards, not the M3 alone. The soak's
    timings under QEMU are its clock's, not a core's, and mean nothing.
  - Not done: the scripts do not pin QEMU or toolchain versions. They are
    recorded here and in `docs/sizes.md` instead.
  - Found on the way: the test harness gave the arena address 64 when its
    heap could not be had, and the soak printed `uint32_t` with `%u`. Both
    fixed.

- 26 October 2026, on `ea1ed4a` plus the uncommitted phase 1 changes:
  **phase 1 done.** `ci/host.sh` passes: 73 cases, the example and both soaks
  under GCC 16 and Clang 21 at C++17 and C++20 with ASan and UBSan fatal, and
  the checked build both ways. `./coverage.sh` is at 100% lines and branches.
  `target/run.sh heltec_v3` passes under QEMU, 73 cases. Clang found two
  things GCC had not: `sort` of an empty range was undefined, now fixed in
  the library, and three hash tests destroyed objects before their table.
  The assert hook is `PRIM_ASSERT_HANDLER`, not a weak default (step 1.3).
- 26 October 2026: toolchains in place (Arm GNU 14.2.rel1, Clang 21.1,
  ESP-IDF 5.3.2 already present).
