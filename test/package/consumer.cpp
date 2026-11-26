//
// prim - Storage Primitives
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// SPDX-License-Identifier: MIT
//

// The installed package works: its headers are found, its version is the one
// asked for (argv[1]), and an arena and a container built from it run.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "prim/arena.h"
#include "prim/vec.h"
#include "prim/version.h"

alignas(8) static uint8_t ram[4096];

int main(int argc, char** argv) {
  if (argc < 2 || std::strcmp(argv[1], PRIM_VERSION) != 0) {
    std::fprintf(stderr, "consumer: built against prim %s, expected %s\n", PRIM_VERSION, argc < 2 ? "?" : argv[1]);
    return 1;
  }
  prim::Arena arena;
  arena.add(ram, sizeof ram, prim::Mem::Fast);
  prim::Vec<uint32_t> v(arena, 100);
  for (uint32_t i = 0; i < 100; ++i)
    if (!v.push(i)) return 1;
  if (v.size() != 100 || v[99] != 99 || arena.check() != 0) return 1;
  std::printf("consumer: prim %s\n", PRIM_VERSION);
  return 0;
}
