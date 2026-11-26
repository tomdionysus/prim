#!/usr/bin/env bash
#
# prim - Storage Primitives
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# SPDX-License-Identifier: MIT
#
# One step of the hosted CI on Debian 13, in GitHub's runner or a local
# container; .github/workflows/ci.yml runs each as its own job. The steps
# call the same scripts a local run does.
#
#   ci/ci.sh check      the header block on every source, clang-format, and
#                       version.h against the release tag
#   ci/ci.sh host       ci/host.sh: GCC and Clang, C++17 and C++20, sanitised
#   ci/ci.sh package    install, then build and run a consumer (test/package)
#   ci/ci.sh coverage   ./coverage.sh's 100% gate; lcov report build/ci/coverage.info
#   ci/ci.sh arm        ci/arm.sh: Cortex-M0, M3, M7 and M33 under QEMU
#
# Builds go under build/ci/. Needs g++ clang cmake ninja-build git jq, and for
# its step clang-format, lcov, or gcc-arm-none-eabi with newlib and
# qemu-system-arm.
#
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd -P)
out=$top/build/ci
step=${1:-}
version=$(sed -n 's/^project(prim VERSION \([0-9.]*\).*/\1/p' "$top/CMakeLists.txt")
mkdir -p "$out"

case "$step" in
  check)
    cd "$top"
    missing=0
    for f in $(git ls-files '*.h' '*.cpp' '*.c' '*.ld' '*.sh' '*.cmake' 'CMakeLists.txt' '*/CMakeLists.txt'); do
      if ! head -8 "$f" | grep -q 'prim - Storage Primitives'; then
        echo "no header block: $f"
        missing=1
      fi
    done
    [ "$missing" = 0 ]
    git ls-files '*.h' '*.cpp' '*.c' | xargs clang-format --dry-run -Werror
    tag=$(git describe --exact-match --tags HEAD 2>/dev/null || true)
    if [ -n "$tag" ] && [ "$tag" != "$version" ]; then
      echo "HEAD is tagged $tag but CMakeLists.txt says $version" >&2
      exit 1
    fi
    echo "check: header blocks, format and version $version"
    ;;
  host)
    "$top/ci/host.sh" "$out/host"
    ;;
  package)
    rm -rf "$out/package"
    cmake -S "$top" -B "$out/package/build" -G Ninja -DCMAKE_INSTALL_PREFIX="$out/package/prefix" >/dev/null
    cmake --install "$out/package/build"
    cmake -S "$top/test/package" -B "$out/package/consumer" -G Ninja -DCMAKE_PREFIX_PATH="$out/package/prefix" \
      -DPRIM_EXPECTED_VERSION="$version" >/dev/null
    cmake --build "$out/package/consumer"
    "$out/package/consumer/consumer" "$version"
    # Added to another project, prim installs nothing and builds no tests.
    mkdir -p "$out/package/parent"
    printf 'cmake_minimum_required(VERSION 3.24)\nproject(parent LANGUAGES CXX)\nadd_subdirectory(%s prim)\n' "$top" \
      >"$out/package/parent/CMakeLists.txt"
    cmake -S "$out/package/parent" -B "$out/package/parent/build" -G Ninja >/dev/null
    if grep -q prim_tests "$out/package/parent/build/build.ninja"; then
      echo "package: add_subdirectory built the tests" >&2
      exit 1
    fi
    ;;
  coverage)
    "$top/coverage.sh" "$out/coverage"
    ignore=inconsistent,inconsistent,mismatch,negative,unused
    branches=(--rc branch_coverage=1)
    lcov --quiet "${branches[@]}" --capture --directory "$out/coverage" --ignore-errors "$ignore" --output-file "$out/all.info"
    lcov --quiet "${branches[@]}" --extract "$out/all.info" "$top/include/prim/*" --ignore-errors "$ignore" \
      --output-file "$out/coverage.info"
    rm "$out/all.info"
    lcov "${branches[@]}" --summary "$out/coverage.info" --ignore-errors "$ignore"
    ;;
  arm)
    "$top/ci/arm.sh"
    ;;
  *)
    echo "usage: ci/ci.sh check|host|package|coverage|arm" >&2
    exit 2
    ;;
esac
