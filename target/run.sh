#!/usr/bin/env bash
#
# The test suite on the ESP32-S3. Builds the test app for a board, then runs
# it under ESP-IDF's QEMU (the default), or flashes it to a board and reads
# its console. Fails unless the suite passes.
#
#   target/run.sh [board]               QEMU (idf_tools.py install qemu-xtensa)
#   target/run.sh [board] <port>        a board: this overwrites its application
#
# board is heltec_v3 (the default), heltec_v4 or tdeck. QEMU models neither
# PSRAM nor native USB, so under it only heltec_v3 is meaningful.
#
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd -P)
board=heltec_v3
if [[ $# -gt 0 && -f $here/sdkconfig.$1 ]]; then
  board=$1
  shift
fi
build=$here/build/$board
. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null 2>&1
idf=(idf.py -C "$here" -B "$build" -DSDKCONFIG="$build/sdkconfig"
     -DSDKCONFIG_DEFAULTS="$here/sdkconfig.defaults;$here/sdkconfig.$board")
"${idf[@]}" build >/dev/null
log=$build/run.log

if [[ $# -eq 0 ]]; then
  size=$(sed -n 's/^CONFIG_ESPTOOLPY_FLASHSIZE="\(.*\)"/\1/p' "$build/sdkconfig")
  (cd "$build" && esptool.py --chip esp32s3 merge_bin --fill-flash-size "$size" -o flash.bin @flash_args >/dev/null)
  qemu=$(find "$HOME/.espressif/tools/qemu-xtensa" -name qemu-system-xtensa -type f | head -1)
  timeout 900 "$qemu" -nographic -machine esp32s3 -drive "file=$build/flash.bin,if=mtd,format=raw" \
    > "$log" 2>&1 &
  pid=$!
  until grep -q '^prim: done' "$log" 2>/dev/null || ! kill -0 $pid 2>/dev/null; do sleep 2; done
  kill $pid 2>/dev/null || true
else
  "${idf[@]}" -p "$1" flash >/dev/null
  timeout 900 "${idf[@]}" -p "$1" monitor --no-reset 2>&1 | tee "$log" | sed '/^prim: done/q' >/dev/null || true
fi

grep -E '^prim:|FAIL|cases' "$log"
grep -q '^prim: done, passed' "$log"
