#pragma once

// PRIM_ASSERT(cond): what the library does when a caller breaks a rule it can
// see, such as pushing an object already on a list or freeing a pointer the
// arena did not give out. The library refuses such a call in every build and
// carries on; PRIM_ASSERT is how a build hears about it.
//
// By default it is nothing. Define PRIM_CHECKED to have it print the file,
// line and condition and abort, or define PRIM_ASSERT yourself, before the
// first prim header, to route it anywhere else.

#ifndef PRIM_ASSERT
#ifdef PRIM_CHECKED
#include <cstdio>
#include <cstdlib>
#define PRIM_ASSERT(c) \
  ((c) ? void(0) : (std::fprintf(stderr, "prim: %s:%d: %s\n", __FILE__, __LINE__, #c), std::abort()))
#else
#define PRIM_ASSERT(c) ((void)0)
#endif
#endif
