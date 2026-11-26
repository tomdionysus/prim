#
# prim - Storage Primitives
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# SPDX-License-Identifier: MIT
#
file(GLOB TESTS ${CMAKE_CURRENT_LIST_DIR}/../../test/*.cpp)
# WHOLE_ARCHIVE: each test file is reached only through its static registrations,
# which the linker would otherwise drop.
# The docs' ESP-IDF code (examples/esp32/) is compiled here too, so it keeps building.
idf_component_register(SRCS app.cpp ${TESTS} ../../examples/esp32/board.cpp INCLUDE_DIRS ../../test REQUIRES prim
                       PRIV_REQUIRES esp_hw_support esp_timer WHOLE_ARCHIVE)
# Two arenas at once must fit in the S3's internal RAM.
target_compile_definitions(${COMPONENT_LIB} PRIVATE PRIM_TEST_HEAP=98304u)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
