#include <cstring>

#include "check.h"

// Every case, or those whose name holds `only`. The host's main and the S3
// test app's app_main (target/) both call it.
int prim_run(const char* only) {
  int run = 0;
  for (check::Case* c = check::head(); c; c = c->next) {
    if (only && !std::strstr(c->name, only)) continue;
    const int before = check::failures();
    c->fn();
    std::printf("  %s %s\n", check::failures() == before ? "ok  " : "FAIL", c->name);
    ++run;
  }
  std::printf("\n%d cases, %d failures\n", run, check::failures());
  return check::failures() || !run ? 1 : 0;  // a run of nothing is not a pass
}

#ifndef ESP_PLATFORM
// prim_tests [name-substring]
int main(int argc, char** argv) { return prim_run(argc > 1 ? argv[1] : nullptr); }
#endif
