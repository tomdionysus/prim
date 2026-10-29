# prim: header-only storage primitives for the ESP32-S3 (README.md). No
# dependencies.
#
# Under idf.py this directory is an ESP-IDF component named `prim`, after its
# directory: point EXTRA_COMPONENT_DIRS at it, or check it out under your
# project's components/, and REQUIRES prim.
#
# On the host it is an interface library, `prim` (alias prim::prim), and its
# test suite, `prim_tests`; build it standalone or add_subdirectory it.
#
#   cmake -S . -B build/host && cmake --build build/host && ./build/host/prim_tests
#   ./coverage.sh             line and branch coverage of each header
#   ci/host.sh                the suite under GCC and Clang, C++17 and C++20, sanitised
#   ./build/host/prim_soak    a simulated day of a node's memory
#   target/run.sh             the suite on the ESP32-S3, under QEMU or on a board

if(ESP_PLATFORM)
  idf_component_register(INCLUDE_DIRS include)
  return()
endif()

cmake_minimum_required(VERSION 3.16)
project(prim VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

option(PRIM_TESTS "Build the test suite, prim_tests, and the README's example" ON)
option(PRIM_COVERAGE "Build the tests for gcov, unoptimised" OFF)
option(PRIM_SANITIZE "Build the tests under ASan and UBSan" OFF)

add_library(prim INTERFACE)
add_library(prim::prim ALIAS prim)
target_include_directories(prim INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_features(prim INTERFACE cxx_std_17)

if(PRIM_TESTS)
  set(PRIM_FLAGS -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
  if(PRIM_COVERAGE)
    list(APPEND PRIM_FLAGS -O0 --coverage)
    add_link_options(--coverage)
  endif()
  if(PRIM_SANITIZE)
    list(APPEND PRIM_FLAGS -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
  endif()

  file(GLOB PRIM_TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/test/*.cpp)
  add_executable(prim_tests ${PRIM_TEST_SRC})
  target_compile_options(prim_tests PRIVATE ${PRIM_FLAGS})
  target_link_libraries(prim_tests PRIVATE prim)

  add_executable(prim_example ${CMAKE_CURRENT_LIST_DIR}/example.cpp)
  target_compile_options(prim_example PRIVATE ${PRIM_FLAGS})
  target_link_libraries(prim_example PRIVATE prim)
endif()

if(PRIM_TESTS)
  # A simulated day of a node's memory (bench/soak.cpp): prim_soak [v3|v4].
  add_executable(prim_soak ${CMAKE_CURRENT_LIST_DIR}/bench/soak.cpp)
  target_compile_options(prim_soak PRIVATE ${PRIM_FLAGS})
  target_link_libraries(prim_soak PRIVATE prim)
endif()
