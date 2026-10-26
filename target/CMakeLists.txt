# The test suite on the ESP32-S3: run.sh builds it for a board and runs it
# under ESP-IDF's QEMU, or flashes it to a board given a port.
#
#   idf.py @heltec_v3.args build      Heltec WiFi LoRa 32 V3 (8 MB flash, no PSRAM)
#   idf.py @heltec_v4.args build      Heltec WiFi LoRa 32 V4 (16 MB flash, 2 MB PSRAM)
#   idf.py @tdeck.args build          LilyGO T-Deck (16 MB flash, 8 MB PSRAM)
#
# The library is the repository root, one directory up, which ESP-IDF takes
# as a component named after that directory: prim.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(CXX_COMPILE_OPTIONS "-std=gnu++20" APPEND)
project(prim_target)
