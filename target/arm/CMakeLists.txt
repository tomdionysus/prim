# The suite as a bare-metal image for a Cortex-M machine under QEMU, with
# output by semihosting (README in startup.c), and the soak beside it.
# ci/arm.sh builds and runs both.
#
#   cmake -S target/arm -B build/arm-m3 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=target/arm/toolchain.cmake -DPRIM_CPU=cortex-m3 -DPRIM_LD=mps2.ld

cmake_minimum_required(VERSION 3.16)
project(prim_arm LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PRIM_CPU cortex-m3 CACHE STRING "the -mcpu to build for")
set(PRIM_LD mps2.ld CACHE STRING "the machine's linker script, in this directory")
set(root ${CMAKE_CURRENT_LIST_DIR}/../..)

set(cpu -mcpu=${PRIM_CPU} -mthumb -mfloat-abi=soft)
add_compile_options(${cpu} -Os -g -ffunction-sections -fdata-sections)
add_link_options(${cpu} --specs=rdimon.specs -nostartfiles -Wl,--gc-sections
                 -L${CMAKE_CURRENT_LIST_DIR} -T${CMAKE_CURRENT_LIST_DIR}/${PRIM_LD})
set(warn -Wall -Wextra -Wpedantic -Werror)
set(cxx -fno-exceptions -fno-rtti)

file(GLOB tests CONFIGURE_DEPENDS ${root}/test/*.cpp)
add_executable(prim_tests.elf ${CMAKE_CURRENT_LIST_DIR}/startup.c ${tests})
target_include_directories(prim_tests.elf PRIVATE ${root}/include ${root}/test)
target_compile_options(prim_tests.elf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${warn} ${cxx}>)

# A simulated day of a node's memory (bench/soak.cpp): prim_soak.elf, given
# v3 or v4 by QEMU's -append. Its timings are QEMU's, not a core's.
add_executable(prim_soak.elf ${CMAKE_CURRENT_LIST_DIR}/startup.c ${root}/bench/soak.cpp)
target_include_directories(prim_soak.elf PRIVATE ${root}/include)
target_compile_options(prim_soak.elf PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${warn} ${cxx}>)
