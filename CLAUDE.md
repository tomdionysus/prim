# prim: working rules

## The library
- include/prim/ is the library. It includes no vendor, RTOS or SDK header.
  Anything that does goes in port/<platform>/.
- C++17. No exceptions, no RTTI, no std:: containers, no recursion, no clock
  reads, no random numbers inside the library.
- Never change a public signature in include/prim/ without being asked. If an
  example needs something the API lacks, stop and say so.
- Read the header before using a primitive. Do not guess an API from its name.

## Tests
- ci/host.sh must pass before any commit: the suite, example and soak under
  GCC and Clang at C++17 and C++20, sanitised, and the checked build's hook.
- ./coverage.sh must report 100% lines and branches for include/prim/.
- A misuse test states its expected reports with CHECK_ASSERTS.
- Randomised tests check against a std:: oracle and take PRIM_SEED.

## Writing
- Docs match the README's voice: short declarative sentences, "octets",
  British spelling, no marketing words, every claim checkable.
- No en or em dashes anywhere: a hyphen for a range, else a colon, a comma or
  a new sentence.
- Code in docs is taken from examples/ by file and line range, never retyped.
- A platform is "supported" only if a script under ci/ builds and runs its
  suite, and the README's table of runs records the date and commit. There
  is no hosted CI.

## Done means
- The task's acceptance commands pass locally.
- CHANGELOG.md has a line under Unreleased.
- The commit message says what was run.
