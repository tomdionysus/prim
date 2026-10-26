#include <cstdio>
#include <cstdlib>

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "prim/arena.h"
#include "sdkconfig.h"

int prim_run(const char* only);

#if CONFIG_PRIM_BOARD_HELTEC_V4
#define PRIM_BOARD "heltec_v4"
#elif CONFIG_PRIM_BOARD_TDECK
#define PRIM_BOARD "tdeck"
#else
#define PRIM_BOARD "heltec_v3"
#endif

// The board's memory as the firmware will give it to an arena: internal RAM
// as fast, PSRAM (on the V4 and the T-Deck) as bulk. Both kinds are given
// and taken back, and the kind rules of alloc.h are checked on the real
// memory map: a fast request is never met from PSRAM, a bulk request takes
// PSRAM when there is some and internal RAM when there is none.
static int board_memory() {
  int bad = 0;
  const uint32_t fast_len = 64 * 1024;
  const uint32_t bulk_len = 256 * 1024;
  void* fast = heap_caps_malloc(fast_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  void* bulk = heap_caps_malloc(bulk_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  std::printf("prim: board %s, internal %u free, psram %u free\n", PRIM_BOARD,
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  if (!fast) {
    std::printf("prim: FAIL no internal RAM for the arena\n");
    return 1;
  }
  prim::Arena a;
  a.add(fast, fast_len, prim::Mem::Fast);
  if (bulk) a.add(bulk, bulk_len, prim::Mem::Bulk);

  void* f = a.alloc(1024, prim::Mem::Fast);
  void* b = a.alloc(1024, prim::Mem::Bulk);
  if (!f || !b) {
    std::printf("prim: FAIL allocation refused (fast %p, bulk %p)\n", f, b);
    ++bad;
  }
  // esp_ptr_external_ram is the chip's own word on where a pointer lies.
  if (f && esp_ptr_external_ram(f)) ++bad, std::printf("prim: FAIL fast block is in PSRAM\n");
  if (b && bulk && !esp_ptr_external_ram(b)) ++bad, std::printf("prim: FAIL bulk block is not in PSRAM\n");
  if (b && !bulk && esp_ptr_external_ram(b)) ++bad, std::printf("prim: FAIL bulk block is in PSRAM on a board without it\n");

  // A bulk arena, if there is one, should hold a block almost its size.
  const prim::Stats sb = a.stats(prim::Mem::Bulk);
  const prim::Stats sf = a.stats(prim::Mem::Fast);
  std::printf("prim: fast %u free (largest %u), bulk %u free (largest %u)\n", unsigned(sf.free),
              unsigned(sf.largest), unsigned(sb.free), unsigned(sb.largest));
  if (bulk && sb.largest < bulk_len - 4096) ++bad, std::printf("prim: FAIL bulk area lost to overhead\n");

  a.free(f);
  a.free(b);
  if (a.used_bytes() != 0) ++bad, std::printf("prim: FAIL %u used after freeing all\n", unsigned(a.used_bytes()));
  if (const int c = a.check()) ++bad, std::printf("prim: FAIL arena check %d\n", c);

  heap_caps_free(bulk);
  heap_caps_free(fast);
  return bad;
}

// The suite in a task with room for its larger cases, then a line run.sh
// waits for.
extern "C" void app_main() {
  xTaskCreate(
      [](void*) {
        std::printf("prim: %u octets of heap free\n", unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
        int rc = board_memory();
        rc |= prim_run(nullptr);
        std::printf("prim: done, %s\n", rc ? "FAILED" : "passed");
        for (;;) vTaskDelay(portMAX_DELAY);
      },
      "prim", 48 * 1024, nullptr, 5, nullptr);
}
