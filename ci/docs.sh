#!/usr/bin/env bash
#
# prim - Storage Primitives
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# SPDX-License-Identifier: MIT
#
# Code in the docs is taken from examples/ by file and line range, never
# retyped. A fenced block that follows a marker line such as
#
#   <!-- code: examples/quick_start.cpp 8-61 -->
#
# must hold exactly those lines of that file, which the host build compiles
# (examples/esp32/ in the test app). Checks every tracked Markdown file and
# fails on the first that differs; with --fix, rewrites each block from its
# file instead.
#
#   ci/docs.sh [--fix]
#
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd -P)
fix=0
[[ ${1:-} == --fix ]] && fix=1
bad=0
for md in $(git -C "$top" ls-files '*.md'); do
  want=$(mktemp)
  awk -v top="$top" -v md="$md" '
    function emit(file, a, b,   line, n) {
      n = 0
      while ((getline line < file) > 0) {
        n++
        if (n >= a && n <= b) print line
      }
      close(file)
      if (n < b || a < 1 || a > b) {
        printf "%s: %s has %d lines, not %d-%d\n", md, file, n, a, b > "/dev/stderr"
        exit 2
      }
    }
    skip { if ($0 ~ /^```/) { skip = 0; print }; next }
    { print }
    /^<!-- code: [^ ]+ [0-9]+-[0-9]+ -->$/ {
      split($0, f, " "); split(f[4], r, "-")
      file = top "/" f[3]; a = r[1] + 0; b = r[2] + 0; pending = 1; next
    }
    pending && /^```/ { emit(file, a, b); skip = 1; pending = 0 }
  ' "$top/$md" >"$want"
  if ! cmp -s "$want" "$top/$md"; then
    if [[ $fix == 1 ]]; then
      cp "$want" "$top/$md"
      echo "docs: rewrote $md from examples/"
    else
      echo "docs: code in $md is not what examples/ holds; ci/docs.sh --fix" >&2
      diff "$top/$md" "$want" | head -20 >&2 || true
      bad=1
    fi
  fi
  rm -f "$want"
done
exit $bad
