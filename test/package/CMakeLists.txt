#
# prim - Storage Primitives
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# SPDX-License-Identifier: MIT
#
# A program that uses prim as an installed package, as a consumer would:
# ci/ci.sh package installs prim, then builds and runs this against it.

cmake_minimum_required(VERSION 3.24)
project(prim_consumer LANGUAGES CXX)

find_package(prim ${PRIM_EXPECTED_VERSION} EXACT CONFIG REQUIRED)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE prim::prim)
target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
