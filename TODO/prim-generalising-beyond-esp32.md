# prim: generalising beyond ESP32

26 October 2026 · Tom Cully

## Summary

prim is already portable in its code; what ties it to the ESP32-S3 is its build flags, its description, its README examples and its test targets. Generalising it is about two weeks of packaging, CI and documentation, not a redesign.

What I checked against the repository as it stands today (commit on `main`, 1,853 lines of headers):

- **The headers use no C++20 features.** The suite builds and passes all 71 cases with `-std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror` on GCC, unchanged. Only `CMakeLists.txt` (`cxx_std_20`) and `target/CMakeLists.txt` (`-std=gnu++20`) ask for C++20.
- **C++14 is two lines away.** The only failures under `-std=c++14` are the two `std::is_trivially_copyable_v` asserts in `vec.h` and `ring.h`.
- **Nothing in `include/` names the ESP32.** ESP-IDF appears only in `target/` and in the README's examples.
- **Two GCC/Clang builtins** (`__builtin_clz` in `arena.h`, `__builtin_clzll` in `trie.h`) are the only compiler-specific code.
- **Runtime division** by a non-power-of-two object size happens in `Pool::put` and `Pool::size_of`. That is a library call on Cortex-M0, which has no divide instruction.
- **HalfSipHash reads its input bytewise**, so it is already safe on cores without unaligned access.

Not verified here: a build with `arm-none-eabi-g++` or IAR, and any run on Arm silicon or QEMU. No Arm toolchain or sysroot was available in this environment; the CI section below closes that gap.

What stays the same: the API, the design, the determinism and checking guarantees, the tests, and ESP32 as the first-class, best-documented target.

## Positioning

prim is for firmware that keeps a changing population of things (peers, sessions, routes, frames, timers) on a microcontroller with tiered memory. It is not a general container library, and it should not try to look like one: ETL owns that default.

**New GitHub description** (replaces the Heltec line):

> Intrusive containers and a checked, accounted allocator for FreeRTOS and bare-metal C++17 on 32-bit MCUs. Header-only, no exceptions, deterministic.

**GitHub topics:** `embedded`, `freertos`, `esp32`, `esp-idf`, `cortex-m`, `stm32`, `rp2040`, `intrusive-containers`, `allocator`, `red-black-tree`, `memory-pool`, `lora`, `meshtastic`, `header-only`, `cpp17`.

**Who it is for, in priority order:**

| Reader | Their problem | What prim gives them |
| --- | --- | --- |
| Mesh and LoRa node developers (Meshtastic, MeshCore, Reticulum-style) | Peer and route tables that grow, churn and fragment the heap | Intrusive hash and tree, pools for frames, a routing trie with longest-prefix match |
| ESP32 IoT and gateway firmware | Internal RAM versus PSRAM, and modules quietly eating memory | Fast and bulk memory with a reserve, per-module accounts, low-water marks |
| Cortex-M firmware with TCM or external SDRAM (STM32H7, i.MX RT) | Hot data must stay in TCM | The same fast/bulk split, mapped to DTCM and SDRAM |
| Teams replacing FreeRTOS `heap_4` | No statistics, no checks on a bad free | A drop-in `pvPortMalloc` that refuses bad frees and reports per kind |
| Safety-minded or long-uptime devices | Heap corruption found months later | Refusal of stray and double frees in every build, `check()` for health tasks |

**Who it is not for,** said plainly in the README: projects that want STL-compatible containers, compile-time fixed capacity with no allocator at all (use ETL), or 8- and 16-bit targets.

**The three claims to lead with,** because no single competitor makes all three: one object can sit in several containers with no allocation; memory is asked for by kind and counted by module; every misuse is refused and reported rather than corrupting state.

## Code changes

Seven changes, none touching the public API. Library code stays free of vendor headers; anything that includes FreeRTOS or a SDK goes under a new `port/` directory beside `include/`, never inside `include/prim/`.

| # | Change | Files | Why | Effort |
| --- | --- | --- | --- | --- |
| 1 | Lower the floor to C++17 | `CMakeLists.txt` (`cxx_std_17`, default standard 17) | Already passes at 17; 20 locks out IAR and older vendor SDKs | 10 min |
| 2 | Portable count-leading-zeros | new `include/prim/bits.h`; `arena.h`, `trie.h` | `__builtin_clz` is GCC/Clang only | 2 h |
| 3 | Divide-free pool indexing | `pool.h` | Runtime `/` and `%` by `size_` are a libgcc call on Cortex-M0 | 3 h |
| 4 | An assert handler hook | `assert.h` | `fprintf` + `abort` is the wrong default on bare metal | 1 h |
| 5 | A shipped locked allocator | new `include/prim/locked.h`, `port/freertos/lock.h` | Every multi-task user writes the same wrapper; it is item 3 of `TODO.md` | 3 h |
| 6 | A FreeRTOS heap replacement | new `port/freertos/heap_prim.cpp` | Lets the kernel itself allocate from an `Arena` | 1 day |
| 7 | Timer tick type as a parameter | `timer.h` | 64-bit deadlines cost more on M0; some users want 32-bit ticks | 3 h, optional |

**1. C++17 floor.** Set `target_compile_features(prim INTERFACE cxx_std_17)` and default `CMAKE_CXX_STANDARD` to 17 when prim is the top-level project. Leave `target/` at `gnu++20`; ESP-IDF 5 defaults to it anyway. Add a CI job that compiles every header at 17 so nothing newer creeps in. Going to C++14 would mean replacing the two `std::is_trivially_copyable_v<T>` with `std::is_trivially_copyable<T>::value`, but no toolchain worth targeting lacks C++17 today, so 17 is the line to hold.

**2. `bits.h`.** Provide `prim::clz32` and `prim::clz64`: `__builtin_clz`/`__builtin_clzll` under GCC and Clang, `__CLZ` from CMSIS or IAR's `__CLZ` intrinsic where defined, and a branch-free de Bruijn fallback otherwise. Both call sites already guard against zero (`arena.h` ORs in 16, `trie.h` tests `x`); keep that contract in a comment.

**3. Pool without division.** `size_` is fixed at construction. When it is a power of two, store its shift and use shifts and masks. Otherwise precompute a 64-bit reciprocal and use Lemire's fastmod, which is two multiplies and exact for 32-bit operands. Both paths are covered by the existing oracle tests; add sizes 8, 24, 40 and 256 to the pool's randomised run.

**4. Assert hook.** Keep `PRIM_ASSERT` as is, but under `PRIM_CHECKED` call `prim_assert_failed(const char* file, int line, const char* cond)`, declared `extern "C"` with a weak default that does today's `fprintf` and `abort`. A Cortex-M user overrides it to log over RTT and call `__BKPT`; an ESP32 user to `ESP_LOGE` and `abort()`.

**5. `Locked<A, Lock>`.** A template `Alloc` adaptor over any `Lock` with `lock()` and `unlock()`, replacing the hand-written class in the README. `port/freertos/lock.h` supplies `FreeRtosMutex` (static mutex, for task context) and `FreeRtosSuspend` (`vTaskSuspendAll`/`xTaskResumeAll`, usable before the scheduler starts). Interrupt handlers still allocate nothing; say so in the header.

**6. `heap_prim.cpp`.** A drop-in replacement for `heap_4.c` on vanilla FreeRTOS (not ESP-IDF, which routes `pvPortMalloc` to `heap_caps`):

- `extern "C"` `pvPortMalloc`, `vPortFree`, `xPortGetFreeHeapSize`, `xPortGetMinimumEverFreeHeapSize`, `vPortGetHeapStats` and `vPortInitialiseBlocks`, with `HeapStats_t` filled from `Stats`.
- Serialised with `vTaskSuspendAll`/`xTaskResumeAll`, as `heap_4` is, so it works before the scheduler starts.
- Kernel requests go to `Mem::Fast`; `prim_heap_add(void*, uint32_t, prim::Mem)` replaces `vPortDefineHeapRegions`.
- Calls `vApplicationMallocFailedHook` when `configUSE_MALLOC_FAILED_HOOK` is 1.
- Exposes `prim_heap()` so application modules can open `Account`s on the same arena the kernel uses.

**7. Tick type.** Make the deadline type `PRIM_TIMER_TICK`, defaulting to `uint64_t`. Document that a 32-bit tick must be a monotonic count the caller rebases before it wraps; the tree orders absolute values and cannot compare across a wrap. Do this only if a Cortex-M0 measurement shows the 64-bit compare matters.

## Build, CI and packaging

Every platform the README names must have a CI job that builds the suite for it and runs it, the way the Xtensa QEMU run backs the ESP32 claim today. A platform without a passing job goes under "should work", not "supported".

**CI matrix (GitHub Actions):**

| Job | Toolchain | Runs on | Proves |
| --- | --- | --- | --- |
| host-gcc | GCC, C++17 and C++20, ASan + UBSan | host | What runs today, at both standards |
| host-clang | Clang, C++17, `-fno-exceptions -fno-rtti` | host | Second compiler, embedded flags |
| coverage | GCC, gcov | host | The existing 100% line and branch gate |
| esp32s3 | ESP-IDF 5.x | QEMU (Xtensa) | Existing `target/run.sh` heltec_v3 |
| cortex-m0 | arm-none-eabi-gcc, `-mcpu=cortex-m0 -Os` | QEMU `microbit` (nRF51) | No divide, no unaligned access, no CLZ instruction |
| cortex-m3 | arm-none-eabi-gcc | QEMU `mps2-an385` | Baseline Cortex-M; also runs the FreeRTOS example |
| cortex-m7 | arm-none-eabi-gcc, `-mcpu=cortex-m7` | QEMU `mps2-an500` | Two arena areas standing in for DTCM and SDRAM |
| cortex-m33 | arm-none-eabi-gcc, `-mcpu=cortex-m33` | QEMU `mps2-an505` | Armv8-M, the RP2350 and nRF54 class |
| size-report | arm-none-eabi-size, xtensa size | build only | Per-target code sizes for the README |

The Arm jobs build `test/` as a bare-metal image with newlib-nano and semihosting for output, so the same 71 cases run unchanged. The `cortex-m3` job also builds `examples/freertos-qemu-heap`, based on the official FreeRTOS `CORTEX_MPS2_QEMU_IAR_GCC` demo, with `heap_prim.cpp` in place of `heap_4.c`. Pin QEMU and toolchain versions in the workflow.

Real boards stay manual and are recorded in a table in the README with date and commit: the three ESP32-S3 boards from `TODO.md`, plus one STM32 Nucleo and one RP2040 or RP2350 board when they are to hand.

**Packaging, so people can install it the way their ecosystem expects:**

| Ecosystem | What to add | Publish with |
| --- | --- | --- |
| ESP-IDF Component Registry | `idf_component.yml` (name, version, description, targets, url, `files.exclude` for `test/`, `bench/`, `target/`) | `compote component upload` from a tag, in CI |
| PlatformIO Registry | `library.json` (`frameworks: espidf, arduino, stm32cube, zephyr`; `build.includeDir: include`; `export.exclude`) | `pio pkg publish` |
| CMake | `install()` and `prim-config.cmake`, plus a documented `FetchContent` snippet | Tags |
| Zephyr | `zephyr/module.yml` and `zephyr/CMakeLists.txt` adding the include path | Tags; users add it to `west.yml` |
| Arduino Library Manager | `library.properties` and a `src/` layout | Later, only if asked for: it forces a layout change |

Cut `v0.2.0` once items 1 to 5 of the code changes and the Arm jobs pass. Keep `CHANGELOG.md` and semver as now; the registries read the version from the tag.

## README rewrite

The current README is a fine reference but a poor front door: the first screen describes the library's rules, and a reader finds out what problem it solves, and whether it runs on their chip, only by scrolling. Keep every word of the reference; move it, and put a front door ahead of it.

**New layout:**

1. Title, one-line description, badges (CI, coverage 100%, ESP Registry, PlatformIO, licence).
2. **Why prim**: the problem in three sentences and the three claims.
3. **Is it for you**: the who-it-is-for table from Positioning, cut to four rows, plus who it is not for.
4. **Thirty seconds**: the existing `example.cpp` session example, unchanged. It is already the best thing on the page.
5. **Install**: one block per ecosystem: ESP-IDF, PlatformIO, CMake `FetchContent`, Zephyr, copy the headers.
6. **Memory at boot, by platform**: the recipes below, one short block each.
7. **Which primitive do I need**: a task-to-header table (keeping things by key → `hash.h`; ordered by key or deadline → `rbtree.h` or `timer.h`; routes by prefix → `trie.h`; many same-size objects → `pool.h`; and so on).
8. **How-tos**: links to the `docs/howto/` pages.
9. **Compared with**: the table below.
10. **Supported targets**: CI-verified and board-verified, with dates.
11. Links: `docs/design.md` (today's Design section), `docs/api.md` (today's API section), `docs/sizes.md` (generated by the size-report job), `docs/testing.md` (today's Tests and Boards sections).

**Draft of the first screen,** in the existing README's voice:

````markdown
# prim

Intrusive containers and a checked, accounted allocator for FreeRTOS and
bare-metal C++17 on 32-bit microcontrollers. Header-only, no exceptions,
no RTTI, no dependencies.

## Why

Firmware that keeps peers, sessions, routes and timers has a population that
comes and goes. On a heap of a few hundred kilobytes that churn fragments
memory, a stray free corrupts it silently, and nobody can say which module
is holding what.

prim answers each of those:

- **One object, many containers, no allocation.** A peer sits in a hash
  table by address, a list by recency and a timer queue by expiry at once.
  Its links live in it.
- **Memory by kind, counted by module.** Ask for fast (internal SRAM, TCM)
  or bulk (PSRAM, SDRAM). Every module allocates through an account that
  knows what it holds and its peak.
- **Misuse refused, never absorbed.** A double free, a stray pointer, an
  object inserted twice: each is refused and reported, in every build.

Tested to 100% line and branch coverage on the host, on Xtensa and on
Cortex-M0, M3, M7 and M33 under QEMU, and on the boards listed below.
````

**Memory at boot, by platform** (one block each in the README; full versions become the how-tos):

| Platform | Fast | Bulk | How the memory is obtained |
| --- | --- | --- | --- |
| ESP32-S3 with PSRAM | Internal RAM | PSRAM | `heap_caps_malloc` with `MALLOC_CAP_INTERNAL` and `MALLOC_CAP_SPIRAM` (today's example) |
| ESP32-S3 / C3 / C6 without PSRAM | Internal RAM | none | Same, fast only |
| STM32H7 | DTCM (128 KB at 0x20000000) | AXI SRAM (512 KB at 0x24000000) or external SDRAM | Static arrays placed by linker section |
| i.MX RT10xx | DTCM (FlexRAM) | OCRAM or SDRAM | Static arrays placed by linker section |
| RP2350 with PSRAM | SRAM | QSPI PSRAM on chip select 1 (mapped at 0x11000000) | A static array for SRAM; the PSRAM window after the board's init |
| nRF52 / nRF53 / RP2040 | SRAM | none | One static array |
| Any vanilla FreeRTOS port | Whatever `prim_heap_add` is given | Whatever `prim_heap_add` is given | `heap_prim.cpp` replaces `heap_4.c` |

The linker section names (`.dtcm_bss`, `.sdram_bss` and so on) differ per project; the recipes say which to look for in a CubeMX or MCUXpresso linker script rather than claiming one name.

**Compared with** (README table):

| | prim | ETL | Boost.Intrusive | BSD `tree.h` / `queue.h` | FreeRTOS `heap_4` | TLSF / o1heap |
| --- | --- | --- | --- | --- | --- | --- |
| Intrusive list | yes | yes | yes | yes | no | no |
| Intrusive hash table | yes | no | yes | no | no | no |
| Intrusive red-black tree | yes | no | yes | yes, unchecked | no | no |
| Allocator | checked, two kinds, accounts | none (fixed capacity) | none | none | first fit | O(1), one kind |
| Misuse refused and reported | yes | partly (asserts) | debug only | no | no | o1heap partly |
| Dependencies | none | none | Boost | none | FreeRTOS | none |
| Language | C++17 | C++03 and up | C++03 and up | C | C | C |

Check the ETL and Boost cells against their current releases before publishing; libraries add containers and these are from memory.

## Examples, quickstarts and how-tos

Three kinds of material, each with one job. Examples are complete projects that CI builds. Quickstarts get one ecosystem from nothing to a running arena in five minutes. How-tos solve one task and link to the example that shows it working.

**Repository layout:**

```
examples/
  host-session/          today's example.cpp, moved
  host-lru-cache/
  esp32-mesh-node/
  freertos-qemu-heap/
  stm32h7-tiers/
  rp2350-psram/
  zephyr-peers/
  platformio-esp32/
docs/
  quickstart/  esp-idf.md  platformio.md  cmake-cortex-m.md  freertos-heap.md  zephyr.md
  howto/       one file per how-to below
  design.md  api.md  sizes.md  testing.md
```

**Examples** (each must build in CI; "runs" means CI executes it and checks its output):

| ID | Example | Platform | Shows | CI |
| --- | --- | --- | --- | --- |
| E1 | host-session | host | One object in a list, a hash table and a timer queue | runs |
| E2 | host-lru-cache | host | `List::move_front` + `Hash` as a bounded cache with eviction | runs |
| E3 | esp32-mesh-node | ESP-IDF, Heltec V3/V4, T-Deck | Peer table, routing trie on a pool, frame pool, one timer queue driving the main loop; radio input simulated so no driver is needed | runs (QEMU, V3) |
| E4 | freertos-qemu-heap | FreeRTOS on QEMU `mps2-an385` | `heap_prim.cpp` replacing `heap_4`; tasks created and deleted; stats per kind and per account printed | runs |
| E5 | stm32h7-tiers | STM32CubeH7, Nucleo-H743ZI | Fast arena in DTCM, bulk in AXI SRAM, kernel heap via `heap_prim` | builds; board run manual |
| E6 | rp2350-psram | Pico SDK 2.x, a board with PSRAM | SRAM as fast, PSRAM as bulk, with the fast reserve | builds; board run manual |
| E7 | zephyr-peers | Zephyr, `qemu_cortex_m3` | prim as a Zephyr module next to Zephyr's own kernel | runs |
| E8 | platformio-esp32 | PlatformIO, `espidf` framework | The minimum `platformio.ini` and `library.json` use | builds |

**Quickstarts** (one page each, under 60 lines, ending with output the reader can compare against):

| Page | From | To |
| --- | --- | --- |
| `esp-idf.md` | `idf.py create-project` | Arena with fast and bulk, a `Hash` of peers, stats logged |
| `platformio.md` | New PlatformIO project | Same, via `lib_deps` |
| `cmake-cortex-m.md` | Any CMake firmware | `FetchContent`, one static arena, `PRIM_CHECKED` and the assert hook wired |
| `freertos-heap.md` | A FreeRTOS project on `heap_4` | `heap_prim.cpp` linked, regions added, `vPortGetHeapStats` showing prim's numbers |
| `zephyr.md` | A west workspace | prim in `west.yml`, built for `qemu_cortex_m3` |

**How-tos** (task titles, as people would search for them):

| How-to | Primitives | Example |
| --- | --- | --- |
| Replace FreeRTOS heap_4 with a checked allocator | `heap_prim`, `Arena` | E4 |
| Keep hot data in internal RAM or TCM and the rest in PSRAM or SDRAM | `Arena`, `Account`, `reserve_fast` | E3, E5, E6 |
| Put one object in several containers | hooks, tags, `linked`, removal order | E1 |
| Build an LRU cache without allocating per entry | `List`, `Hash` | E2 |
| Expire peers with one timer queue and a sleeping loop | `TimerQueue`, `ulTaskNotifyTake` | E3 |
| Route by longest prefix | `Trie`, `Pool` | E3 |
| Share an arena between FreeRTOS tasks | `Locked`, `FreeRtosMutex` | E4 |
| Find which module is holding memory | `Account::bytes`, `peak`, `Stats::least` | E3, E4 |
| Catch heap corruption in the field | `PRIM_CHECKED`, `prim_assert_failed`, `check()` in a health task | E4 |
| Hash keys a peer can choose, safely | `hash_seed`, `hash32` | E3 |
| Stop frame churn fragmenting the heap | `Pool` | E3 |
| Move from ETL or `std::` containers | mapping table, ownership rules | none |

Every how-to follows one template: the problem in two sentences; the code, taken from the example by file and line range rather than pasted copies that drift; what to check to know it worked; the rules that bite (removal before free, destruction order, no allocation in interrupts).

## Directions for a Claude agent

An agent does this work well when three things are fixed in the repository rather than in each prompt: the house rules, a brief per task, and a check it can run to know it is done. Commit the `CLAUDE.md` below first; then hand the agent one brief at a time, each as its own branch and pull request.

**`CLAUDE.md` for the repository root:**

````markdown
# prim: working rules

## The library
- include/prim/ is the library. It includes no vendor, RTOS or SDK header.
  Anything that does goes in port/<platform>/.
- C++17. No exceptions, no RTTI, no std:: containers, no recursion,
  no clock reads, no random numbers inside the library.
- Never change a public signature in include/prim/ without being asked.
  If an example needs something the API lacks, stop and say so.
- Read the header before using a primitive. Do not guess an API from its name.

## Tests
- cmake -S . -B build/host -DPRIM_SANITIZE=ON && cmake --build build/host
  && ./build/host/prim_tests must pass before any commit.
- ./coverage.sh must report 100% lines and branches for include/prim/.
- A misuse test states its expected reports with CHECK_ASSERTS.
- Randomised tests check against a std:: oracle and take PRIM_SEED.

## Writing
- Docs match the README's voice: short declarative sentences, "octets",
  British spelling, no marketing words, every claim checkable.
- Code in docs is taken from examples/ by file and line range
  (a snippet marker), never retyped.
- A platform is "supported" only if a CI job builds and runs it.

## Done means
- The task's acceptance commands pass locally and in CI.
- CHANGELOG.md has a line under Unreleased.
- The PR description lists what was run and its output.
````

**Brief template** (one per task; paste into the agent with the repository attached):

````markdown
## Task: <ID> <title>
Read first: CLAUDE.md, README.md, <headers it uses>, <related example>.
Build: <directory and files to create>.
Behaviour: <what the program does, step by step>.
Output: <exact lines it prints, or the pattern CI greps for>.
Acceptance:
- <command> exits 0
- <command> prints <pattern>
- CI job <name> added and green
Out of scope: <what not to touch>.
Stop and ask if: <conditions>.
````

**Brief E4, FreeRTOS heap replacement on QEMU** (the first one to run, since it also produces `heap_prim.cpp`):

````markdown
## Task: E4 freertos-qemu-heap
Read first: CLAUDE.md, include/prim/arena.h, alloc.h, pool.h,
the FreeRTOS Demo/CORTEX_MPS2_QEMU_IAR_GCC tree (pin the kernel tag).
Build:
- port/freertos/heap_prim.cpp: pvPortMalloc, vPortFree,
  xPortGetFreeHeapSize, xPortGetMinimumEverFreeHeapSize,
  vPortGetHeapStats, vPortInitialiseBlocks, prim_heap_add, prim_heap.
  Serialise with vTaskSuspendAll/xTaskResumeAll. Kernel requests are
  Mem::Fast. Call vApplicationMallocFailedHook when configured.
- examples/freertos-qemu-heap/: the MPS2 demo with heap_4.c removed and
  heap_prim.cpp linked; two arena regions of 64 KB (fast) and 128 KB (bulk).
Behaviour: create 8 tasks that each allocate through their own Account,
free, and delete themselves; a monitor task prints stats every second for
5 seconds, runs arena.check(), then prints PASS or FAIL and exits QEMU
through semihosting.
Output: lines "fast free=<n> least=<n>", "bulk free=<n> least=<n>",
"check=0", and finally "PASS".
Acceptance:
- make -C examples/freertos-qemu-heap builds with arm-none-eabi-gcc
- qemu-system-arm -M mps2-an385 -nographic -semihosting ... prints PASS
  within 30 s
- a vPortFree of a stack address increments bad_frees and does not crash
- CI job cortex-m3 runs it
Out of scope: changes to include/prim/.
Stop and ask if: the kernel calls pvPortMalloc from an interrupt in this
port, or the demo needs a kernel tag older than V10.4.
````

**Brief E3, ESP32 mesh node:**

````markdown
## Task: E3 esp32-mesh-node
Read first: CLAUDE.md, README (Memory at boot, Timers and the main loop),
hash.h, list.h, timer.h, trie.h, pool.h, target/ (board args and sdkconfig).
Build: examples/esp32-mesh-node/, an ESP-IDF project reusing target/'s
board args. No radio driver: a simulated radio task posts frames of random
peers and routes at 20 Hz from a seeded generator.
Behaviour: frames come from a Pool; peers live in a Hash by id, a List by
recency and a TimerQueue for 30 s expiry; routes live in a Trie on a Pool;
the main loop sleeps until the next deadline with ulTaskNotifyTake. Every
10 s log per-Account bytes and peak, and per-kind free, largest and least.
Output: after 120 simulated seconds print peers, routes, failures=0,
bad_frees=0, check=0, PASS.
Acceptance:
- idf.py @heltec_v3.args build, and the same for heltec_v4 and tdeck
- target/run.sh-style QEMU run for heltec_v3 prints PASS
- CI job esp32s3 runs it
Out of scope: any real radio, Meshtastic protocol compatibility.
Stop and ask if: memory_init in the README does not fit the QEMU heap.
````

**The other briefs** differ from these in a few fields only:

| ID | Read first (beyond CLAUDE.md) | Acceptance | Stop and ask if |
| --- | --- | --- | --- |
| E1 | `example.cpp` | Moved to `examples/host-session/`, still built and run by the host CMake | Never; it is a move |
| E2 | `list.h`, `hash.h` | Host run evicts in LRU order for 10,000 random ops against a `std::list` + `std::unordered_map` oracle | The cache needs ownership the containers do not give |
| E5 | E4's `heap_prim.cpp`, the H743 CubeMX linker script | `cmake --preset h743` builds; map file shows the fast area in DTCM and the bulk area in AXI SRAM | Linker sections need hand-editing CubeMX would overwrite |
| E6 | Pico SDK 2.x PSRAM setup for the chosen board | Builds for `pico2`; board run recorded by hand | The board's PSRAM init is not in the SDK |
| E7 | Zephyr module docs, `zephyr/module.yml` | `west build -b qemu_cortex_m3` and `west build -t run` print PASS | prim would need Zephyr's allocator rather than its own |
| E8 | `library.json` | `pio run` builds for `heltec_wifi_lora_32_V3` | The PlatformIO board id differs |

**How-to brief:** "Write `docs/howto/<slug>.md` from example <ID>. Follow the how-to template in the plan. Every code block is a snippet marker into `examples/<ID>/`. Under 120 lines. Then run the docs link check." Add a CI step that resolves snippet markers and fails on a broken range, so docs cannot drift from code.

Run briefs one at a time and review each pull request; let the agent run the acceptance commands itself, and treat a brief it cannot satisfy as a question back, not a failure to hide.

## Order of work

Five phases, each ending in something shippable. Roughly ten working days in total, most of it CI and writing.

1. **Foundations (day 1).** Commit `CLAUDE.md`. Code changes 1, 2 and 4: C++17 floor, `bits.h`, assert hook. Add the host-clang job. *Done when:* host suite and coverage pass at C++17 and C++20 on GCC and Clang.
2. **Arm in CI (days 2–3).** Bare-metal test image for Cortex-M; jobs cortex-m0, m3, m7, m33; code change 3 (divide-free pool) landed against the M0 job. *Done when:* all 71 cases pass on all four under QEMU, and the size-report job publishes a table.
3. **FreeRTOS (days 4–5).** Code changes 5 and 6; brief E4. *Done when:* E4 prints PASS in CI and the bad-free check holds. Cut `v0.2.0`.
4. **Front door (days 6–7).** README rewrite; move Design, API, Tests to `docs/`; new GitHub description and topics; `idf_component.yml` and `library.json`; publish to both registries. *Done when:* a stranger can install from either registry by following the README alone. Test this with a fresh agent that has only the README.
5. **Examples and how-tos (days 8–10).** Briefs E1, E2, E3, E7, E8 first (all run or build in CI), then E5 and E6, then the how-tos in the order of the table. *Done when:* every how-to links to a CI-built example and the snippet check passes.

The outreach plan in the sibling document starts after phase 4: there is no point sending people to a README that still says Heltec on its first line.

**Open questions:**

- Is the API stable enough to call `v1.0` after phase 5, or should it stay `0.x` until someone outside uses it?
- Which STM32 and RP2350 boards are to hand, for the manual board runs?
- Does the timer tick type (code change 7) matter to anyone? Leave it until an M0 user asks.
