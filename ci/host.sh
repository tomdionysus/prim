#!/usr/bin/env bash
#
# The host matrix: the suite, the README's example and the soak under GCC and
# Clang, at C++17 and C++20, under ASan and UBSan; then the checked build
# (assert.h), printing and aborting, and calling the program's own handler.
# Prints a line per configuration and ends with the line the README's table
# of runs takes: date and commit.
#
#   ci/host.sh [build-root]     default build/ci
#
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd -P)
root=${1:-$here/build/ci}
compilers=(g++ clang++)
# A sanitiser report fails the run, not just prints.
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS=halt_on_error=1
for cc in "${compilers[@]}"; do
  command -v "$cc" >/dev/null || { echo "$cc: not installed" >&2; exit 1; }
done

for cc in "${compilers[@]}"; do
  for std in 17 20; do
    b=$root/host-$cc-$std
    cmake -S "$here" -B "$b" -G Ninja -DCMAKE_CXX_COMPILER="$cc" -DCMAKE_CXX_STANDARD=$std -DPRIM_SANITIZE=ON >/dev/null
    cmake --build "$b" >"$b/build.log" 2>&1 || { echo "FAIL $cc -std=c++$std: build; see $b/build.log" >&2; grep -m5 -A3 "error" "$b/build.log" >&2; exit 1; }
    if ! "$b/prim_tests" >"$b/tests.log" 2>&1; then
      echo "FAIL $cc -std=c++$std: $(tail -1 "$b/tests.log")" >&2
      grep -E "FAIL|ERROR|runtime error" "$b/tests.log" | head -10 >&2
      exit 1
    fi
    cases=$(tail -1 "$b/tests.log")
    "$b/prim_example"
    "$b/prim_soak" v3 >/dev/null
    "$b/prim_soak" v4 >/dev/null
    echo "ok   $cc -std=c++$std sanitised: $cases; example; soak v3 and v4"
  done
done

flags=(-std=c++17 -I "$here/include" -DPRIM_CHECKED -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
for cc in "${compilers[@]}"; do
  "$cc" "${flags[@]}" "$here/ci/checked.cpp" -o "$root/checked-$cc"
  out=$("$root/checked-$cc" 2>&1 || true)  # it aborts
  grep -q "^prim: .*list.h:[0-9]*: " <<<"$out" || { echo "$cc: PRIM_CHECKED did not report: $out" >&2; exit 1; }
  "$cc" "${flags[@]}" -DPRIM_ASSERT_HANDLER "$here/ci/checked.cpp" -o "$root/checked-own-$cc"
  "$root/checked-own-$cc" | grep -q "^handled 1$" || { echo "$cc: own handler not called" >&2; exit 1; }
  echo "ok   $cc PRIM_CHECKED: prints and aborts; with PRIM_ASSERT_HANDLER the program's own function is called"
done

echo "host: pass $(date -I) $(git -C "$here" rev-parse --short HEAD)"
