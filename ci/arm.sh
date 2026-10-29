#!/usr/bin/env bash
#
# The suite and the soak on Cortex-M, under QEMU's MPS2 machines, as
# bare-metal images with output by semihosting (target/arm/). For each core:
# build, run, and pass only if QEMU exits 0, the suite reports no failures
# and the soak passes as the V3 and as the V4. Ends with the
# line the README's table of runs takes: date and commit.
#
#   ci/arm.sh [core ...]     default: m0 m3 m7 m33
#
# m0 is built for the Cortex-M0 (ARMv6-M: no divide, no CLZ) and run on the
# AN385's M3, which executes ARMv6-M code unchanged. It does not trap an
# unaligned access as an M0 would; the host's UBSan checks alignment.
#
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd -P)
command -v arm-none-eabi-g++ >/dev/null || { echo "arm-none-eabi-g++: not installed" >&2; exit 1; }
command -v qemu-system-arm >/dev/null || { echo "qemu-system-arm: not installed" >&2; exit 1; }

cores=("$@")
[[ ${#cores[@]} -gt 0 ]] || cores=(m0 m3 m7 m33)
for core in "${cores[@]}"; do
  case $core in
    m0)  cpu=cortex-m0  machine=mps2-an385 ld=mps2.ld ;;
    m3)  cpu=cortex-m3  machine=mps2-an385 ld=mps2.ld ;;
    m7)  cpu=cortex-m7  machine=mps2-an500 ld=mps2.ld ;;
    m33) cpu=cortex-m33 machine=mps2-an505 ld=an505.ld ;;
    *) echo "unknown core: $core" >&2; exit 1 ;;
  esac
  b=$here/build/arm-$core
  cmake -S "$here/target/arm" -B "$b" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$here/target/arm/toolchain.cmake" \
    -DPRIM_CPU=$cpu -DPRIM_LD=$ld >/dev/null
  cmake --build "$b" >"$b/build.log" 2>&1 || { echo "FAIL $core: build; see $b/build.log" >&2; grep -m5 -A3 error "$b/build.log" >&2; exit 1; }
  run() {  # log image [append]
    local rc=0
    timeout 1800 qemu-system-arm -M $machine -nographic -semihosting-config enable=on,target=native \
      -kernel "$b/$2" ${3:+-append "$3"} >"$b/$1" 2>&1 || rc=$?
    return $rc
  }
  rc=0
  run run.log prim_tests.elf || rc=$?
  result=$(grep -E '^[0-9]+ cases, [0-9]+ failures$' "$b/run.log" || true)
  if [[ $rc -ne 0 || $result != *" 0 failures" ]]; then
    echo "FAIL $core ($cpu on $machine): qemu exit $rc, ${result:-no summary}; see $b/run.log" >&2
    grep -E 'FAIL|fatal' "$b/run.log" | head -5 >&2 || true
    exit 1
  fi
  # The soak, as each board: its checks and that nothing is left allocated.
  # The timings it prints are QEMU's clock, not the core's.
  for board in v3 v4; do
    rc=0
    run soak-$board.log prim_soak.elf $board || rc=$?
    if [[ $rc -ne 0 ]] || [[ $(tail -1 "$b/soak-$board.log") != passed ]]; then
      echo "FAIL $core soak $board: qemu exit $rc; see $b/soak-$board.log" >&2
      tail -3 "$b/soak-$board.log" >&2
      exit 1
    fi
  done
  echo "ok   $core: $cpu on $machine: $result; soak v3 and v4"
done

echo "arm: pass $(date -I) $(git -C "$here" rev-parse --short HEAD)"
