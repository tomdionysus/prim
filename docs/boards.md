# prim: boards

`target/` is an ESP-IDF project that builds the suite as an application for
one of three boards, selected by an argument file that layers the board's
`sdkconfig.<board>` over `sdkconfig.defaults`:

| Board | Chip | Flash | PSRAM | Console | Build |
| --- | --- | --- | --- | --- | --- |
| Heltec WiFi LoRa 32 V3 | ESP32-S3FN8 | 8 MB | none | UART0 (CP2102) | `idf.py @heltec_v3.args build` |
| Heltec WiFi LoRa 32 V4 | ESP32-S3R2 | 16 MB | 2 MB quad | USB-serial-JTAG | `idf.py @heltec_v4.args build` |
| LilyGO T-Deck | ESP32-S3FN16R8 | 16 MB | 8 MB octal | USB-serial-JTAG | `idf.py @tdeck.args build` |

Before the suite, the application gives an arena the board's own memory,
internal RAM as fast and PSRAM as bulk, allocates from each and checks with
`esp_ptr_external_ram` that a fast request was never met from PSRAM and a
bulk request was met from it where there is any. On the V3 bulk requests
fall through to internal RAM, as `alloc.h` says they do.

`target/run.sh [board]` builds and runs under QEMU (install it with
`idf_tools.py install qemu-xtensa`); QEMU models neither PSRAM nor native USB,
so there the V3 is the one to run. `target/run.sh <board> <port>` flashes a
board instead, overwriting whatever application it holds, and reads the
result from its console. Either fails unless the suite passes.
