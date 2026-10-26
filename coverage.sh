#!/usr/bin/env bash
#
# Line and branch coverage of each primitive, from gcov, unoptimised. A
# header's templates are compiled into several files: a line counts as run,
# and a branch as taken, if any copy of it was. Prints each file's figures
# and every line or branch missed, and fails unless all are at 100%.
#
#   ./coverage.sh [build-dir]     default build/coverage
#
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd -P)
build=${1:-$here/build/coverage}

cmake -S "$here" -B "$build" -G Ninja -DPRIM_COVERAGE=ON >/dev/null
cmake --build "$build" >/dev/null
find "$build" -name '*.gcda' -delete
"$build/prim_tests" >/dev/null

cd "$build"
find . -name '*.gcda' -print0 | xargs -0 gcov --branch-probabilities --json-format --stdout 2>/dev/null |
  jq -s -r --arg root "$here/" '
    [ .[].files[]
      | select((.file | startswith($root)) and (.file | contains("/test/") | not))
      | (.file | ltrimstr($root)) as $f
      | .lines[]
      | {f: $f, l: .line_number, run: (.count > 0), b: [.branches[] | .count > 0]} ]
    | group_by(.f)[]
    | .[0].f as $f
    | [ group_by(.l)[]
        | { l: .[0].l,
            run: any(.[]; .run),
            b: ((map(.b | length) | max) as $n | [range(0; $n) as $k | any(.[]; .b[$k] // false)]) } ] as $lines
    | ($lines | map(.b[]) ) as $br
    | ([$lines[] | select(.run)] | length) as $lr
    | ($br | map(select(.)) | length) as $bt
    | "\($f): lines \($lr)/\($lines | length), branches \($bt)/\($br | length)",
      ($lines[] | select((.run | not) or (.b | any(. | not)))
       | "    \($f):\(.l) " + (if .run then "branch not taken: \(.b | map(if . then "+" else "-" end) | join(""))" else "not run" end))
  ' | tee coverage.txt
! grep -q '^    ' coverage.txt
