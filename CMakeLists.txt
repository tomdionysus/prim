#
# prim - Storage Primitives
#
# Copyright (C) 2026 Tom Cully <mail@tomcully.com>
# SPDX-License-Identifier: MIT
#
# prim: header-only storage primitives for microcontrollers (README.md). No
# dependencies.
#
# Under idf.py this directory is an ESP-IDF component named `prim`, after its
# directory: point EXTRA_COMPONENT_DIRS at it, or check it out under your
# project's components/, and REQUIRES prim.
#
# On the host it is an interface library, `prim` (alias prim::prim): build it
# standalone, add_subdirectory it, or install it and find_package(prim 0.1 CONFIG).
#
#   cmake -S . -B build/host -DPRIM_BUILD_TESTING=ON -DPRIM_WARNINGS_AS_ERRORS=ON
#   cmake --build build/host && ./build/host/prim_tests
#   ./coverage.sh             line and branch coverage of each header
#   ci/host.sh                the suite under GCC and Clang, C++17 and C++20, sanitised
#   ci/ci.sh <step>           one step of the hosted CI (.github/workflows/ci.yml)
#   ./build/host/prim_soak    a simulated day of a node's memory
#   target/run.sh             the suite on the ESP32-S3, under QEMU or on a board

if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include)
  return()
endif()

cmake_minimum_required(VERSION 3.24)
project(prim VERSION 0.1.0 LANGUAGES CXX)

# include/prim/version.h is what a program sees; it must say what project() says.
file(STRINGS ${PROJECT_SOURCE_DIR}/include/prim/version.h PRIM_VERSION_LINE REGEX "^#define PRIM_VERSION \"")
if(NOT PRIM_VERSION_LINE STREQUAL "#define PRIM_VERSION \"${PROJECT_VERSION}\"")
  message(FATAL_ERROR "include/prim/version.h says ${PRIM_VERSION_LINE}; project() says ${PROJECT_VERSION}")
endif()

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
  endif()
endif()

option(PRIM_BUILD_TESTING "Build the suite, prim_tests, the README's example and the soak" OFF)
option(PRIM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PRIM_COVERAGE "Build the tests for gcov, unoptimised" OFF)
option(PRIM_SANITIZE "Build the tests under ASan and UBSan" OFF)
option(PRIM_INSTALL "Install the headers and the CMake package" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

add_library(prim INTERFACE)
add_library(prim::prim ALIAS prim)
target_include_directories(prim INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                          $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(prim INTERFACE cxx_std_17)

if(PRIM_INSTALL)
  include(CMakePackageConfigHelpers)
  set(PRIM_CMAKE_DIR ${CMAKE_INSTALL_DATADIR}/cmake/prim)
  install(DIRECTORY include/prim DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS prim EXPORT prim-targets)
  install(EXPORT prim-targets NAMESPACE prim:: DESTINATION ${PRIM_CMAKE_DIR})
  write_basic_package_version_file(${PROJECT_BINARY_DIR}/prim-config-version.cmake
                                   COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
  install(FILES cmake/prim-config.cmake ${PROJECT_BINARY_DIR}/prim-config-version.cmake DESTINATION ${PRIM_CMAKE_DIR})
endif()

if(PRIM_BUILD_TESTING)
  # The flags every test, example and tool is built with. The library is
  # headers, so this is what holds it to the warnings, and to building without
  # exceptions or RTTI.
  add_library(prim_warnings INTERFACE)
  target_compile_options(prim_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
                                                 -fno-exceptions -fno-rtti)
  if(PRIM_WARNINGS_AS_ERRORS)
    target_compile_options(prim_warnings INTERFACE -Werror)
  endif()
  if(PRIM_COVERAGE)
    target_compile_options(prim_warnings INTERFACE -O0 --coverage)
    target_link_options(prim_warnings INTERFACE --coverage)
  endif()
  if(PRIM_SANITIZE)
    target_compile_options(prim_warnings INTERFACE -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(prim_warnings INTERFACE -fsanitize=address,undefined)
  endif()

  file(GLOB PRIM_TEST_SRC CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/test/*.cpp)
  add_executable(prim_tests ${PRIM_TEST_SRC})
  target_link_libraries(prim_tests PRIVATE prim prim_warnings)

  add_executable(prim_example ${PROJECT_SOURCE_DIR}/examples/quick_start.cpp)
  target_link_libraries(prim_example PRIVATE prim prim_warnings)

  # A simulated day of a node's memory (bench/soak.cpp): prim_soak [v3|v4].
  add_executable(prim_soak ${PROJECT_SOURCE_DIR}/bench/soak.cpp)
  target_link_libraries(prim_soak PRIVATE prim prim_warnings)
endif()
