#pragma once

// PRIM_ASSERT(cond): what the library does when a caller breaks a rule it can
// see, such as pushing an object already on a list or freeing a pointer the
// arena did not give out. The library refuses such a call in every build and
// carries on; PRIM_ASSERT is how a build hears about it.
//
// By default it is nothing. Define PRIM_CHECKED to have each refusal print
// the file, line and condition and abort. Define PRIM_ASSERT_HANDLER as well
// to have it call prim_assert_failed(file, line, cond) instead, a function
// the program defines once, extern "C", to log over a debug channel, break
// into a debugger or count. Or define PRIM_ASSERT yourself, before the first
// prim header, to route it anywhere else. Whichever is chosen, every file of
// the program is compiled with the same.

#ifndef PRIM_ASSERT
#if defined(PRIM_CHECKED) && defined(PRIM_ASSERT_HANDLER)
extern "C" void prim_assert_failed(const char* file, int line, const char* cond);
#define PRIM_ASSERT(c) ((c) ? void(0) : prim_assert_failed(__FILE__, __LINE__, #c))
#elif defined(PRIM_CHECKED)
#include <cstdio>
#include <cstdlib>
#define PRIM_ASSERT(c) \
  ((c) ? void(0) : (std::fprintf(stderr, "prim: %s:%d: %s\n", __FILE__, __LINE__, #c), std::abort()))
#else
#define PRIM_ASSERT(c) ((void)0)
#endif
#endif
