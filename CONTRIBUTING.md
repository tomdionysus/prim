# Contributing to prim

The rules a change to prim keeps. They exist so the library stays small,
predictable and fit for a microcontroller.

## The library

- `include/prim/` is the library. It includes no vendor, RTOS or SDK
  header; anything that does belongs in a program or a port, not here.
- C++17. No exceptions, no RTTI, no `std::` containers, no recursion, no
  clock reads and no random numbers inside the library.
- A misuse is refused and reported through `PRIM_ASSERT`, in every build.
  Nothing throws.
- A public signature changes only with a minor release while prim is 0.x,
  and the change is recorded in `CHANGELOG.md`.

## Style

- `.clang-format` is authoritative: `clang-format -i` the files you change.
- Google style at 120 columns, `.h` headers, `snake_case` functions and
  variables, `PascalCase` types, `member_` for private members, `kName` for
  constants, everything in `namespace prim`.
- Every source file opens with the header block:

      //
      // prim - Storage Primitives
      //
      // Copyright (C) 2026 Tom Cully <mail@tomcully.com>
      // SPDX-License-Identifier: MIT
      //

## Tests

- Every change keeps these passing:

      ci/host.sh       the suite, the examples and the soak under GCC and
                       Clang, C++17 and C++20, ASan and UBSan, and the
                       checked build's hook
      ./coverage.sh    100% of lines and branches of include/prim/
      ci/ci.sh check   header blocks, format, docs' code, version

- A test that breaks a rule on purpose states the reports it expects with
  `CHECK_ASSERTS`.
- A randomised test checks against a `std::` oracle and takes `PRIM_SEED`.
- `ci/arm.sh` (Cortex-M under QEMU) and `target/run.sh` (the ESP32-S3)
  run before a release. The hosted CI runs everything but the real boards.

## Docs

- `README.md` is the overview; everything else is in `docs/`, which states
  what is, not history or plans. Plans are in `TODO/`.
- Code in the docs is taken from `examples/` by file and line range, never
  retyped. `ci/docs.sh` checks it and `ci/docs.sh --fix` copies it in.
- Short declarative sentences, "octets", British spelling, no marketing
  words, every claim checkable. No en or em dashes.
- A platform is supported only if a script under `ci/` builds and runs the
  suite on it, and the README's table of runs records the date and commit.

## Commits and releases

- Work happens on `develop`; `main` is the last release, reached by merging
  `develop`.
- One change a commit, with a short imperative subject. A commit that moves
  files does not change them. The message says what was run.
- `CHANGELOG.md` has a line under Unreleased for every change.
- A release is an annotated tag `x.y.z`, no `v`, cut from `main`. The tag,
  `project(prim VERSION ...)` and `prim/version.h` are one number; CMake
  refuses to configure if the last two differ, and `ci/ci.sh check` fails a
  tagged commit whose tag differs.
