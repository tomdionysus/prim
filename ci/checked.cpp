// The checked build (assert.h). Built with -DPRIM_CHECKED, a refusal prints
// "prim: file:line: condition" and aborts; with PRIM_ASSERT_HANDLER as well
// it calls prim_assert_failed, which this file defines to count the call.
// ci/host.sh builds and runs both.

#include <cstdio>

#include "prim/list.h"

#ifdef PRIM_ASSERT_HANDLER
static int handled = 0;
extern "C" void prim_assert_failed(const char*, int, const char*) { ++handled; }
#endif

namespace {
struct Item : prim::ListHook<> {};
}  // namespace

int main() {
  prim::List<Item> l;
  Item a;
  l.push_front(a);
  l.push_front(a);  // refused: already on a list
#ifdef PRIM_ASSERT_HANDLER
  std::printf("handled %d\n", handled);
  return handled == 1 ? 0 : 1;
#else
  std::puts("not reached");
  return 1;
#endif
}
