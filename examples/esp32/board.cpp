//
// prim - Storage Primitives
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// SPDX-License-Identifier: MIT
//

// The guide's code for an ESP32-S3 program (docs/guide.md shows each part by
// line range): memory at boot, the main loop, watching memory and a locked
// allocator. The test app compiles it (target/main/CMakeLists.txt), so it
// keeps building against ESP-IDF; nothing calls it.

#include <cstdint>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "prim/alloc.h"
#include "prim/arena.h"
#include "prim/siphash.h"
#include "prim/timer.h"

namespace board_example {

// Memory at boot.
prim::Arena g_arena;

void memory_init() {
  const uint32_t fast_len = 96 * 1024;
  g_arena.add(heap_caps_malloc(fast_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), fast_len, prim::Mem::Fast);

  // On a V4 or a T-Deck; on a V3 there is none, and the call adds nothing.
  const uint32_t bulk_len = uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)) / 2;
  if (bulk_len) g_arena.add(heap_caps_malloc(bulk_len, MALLOC_CAP_SPIRAM), bulk_len, prim::Mem::Bulk);

  // Bulk requests fall back to fast memory only while this much stays free.
  g_arena.reserve_fast(16 * 1024);

  uint8_t key[8];
  esp_fill_random(key, sizeof key);
  prim::hash_seed(key);
}

// Timers and the main loop.
struct Expiry;
struct Peer : prim::TimerHook<Expiry> {
  uint32_t id;
};

prim::TimerQueue g_timers;

void expire(prim::Timer& t) {
  Peer& p = static_cast<Peer&>(static_cast<prim::TimerHook<Expiry>&>(t));
  // ... take p out of its containers and free it
  (void)p;
}

void heard(Peer& p, uint64_t now_ms) {
  static_cast<prim::TimerHook<Expiry>&>(p).fire = expire;
  g_timers.arm(p, now_ms + 30000);  // re-arms it if it was armed
}

void main_loop() {
  for (;;) {
    const uint64_t now = uint64_t(esp_timer_get_time()) / 1000;  // ms
    g_timers.run(now);
    const uint64_t next = g_timers.next();
    const uint64_t wait = next == prim::TimerQueue::kNever ? 1000 : next > now ? next - now : 0;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(uint32_t(wait)));  // or until another task wakes it
  }
}

// Watching memory.
void memory_report() {
  const prim::Stats fast = g_arena.stats(prim::Mem::Fast);
  const prim::Stats bulk = g_arena.stats(prim::Mem::Bulk);
  ESP_LOGI("mem", "fast %u free, %u largest, %u least", unsigned(fast.free), unsigned(fast.largest),
           unsigned(fast.least));
  ESP_LOGI("mem", "bulk %u free, %u largest, %u least", unsigned(bulk.free), unsigned(bulk.largest),
           unsigned(bulk.least));
  ESP_LOGI("mem", "refused %u, bad frees %u", unsigned(g_arena.failures()), unsigned(g_arena.bad_frees()));
}

// Several tasks.
class Locked final : public prim::Alloc {
 public:
  explicit Locked(prim::Alloc& to) : to_(to), m_(xSemaphoreCreateMutexStatic(&buf_)) {}
  void* alloc(uint32_t n, prim::Mem want = prim::Mem::Bulk) override {
    Hold h(m_);
    return to_.alloc(n, want);
  }
  void* resize(void* p, uint32_t n) override {
    Hold h(m_);
    return to_.resize(p, n);
  }
  void free(void* p) override {
    Hold h(m_);
    to_.free(p);
  }
  uint32_t size_of(const void* p) const override {
    Hold h(m_);
    return to_.size_of(p);
  }
  prim::Stats stats(prim::Mem kind) const override {
    Hold h(m_);
    return to_.stats(kind);
  }

 private:
  struct Hold {
    SemaphoreHandle_t m;
    explicit Hold(SemaphoreHandle_t m) : m(m) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Hold() { xSemaphoreGive(m); }
  };
  prim::Alloc& to_;
  StaticSemaphore_t buf_;
  SemaphoreHandle_t m_;
};

// At boot, after memory_init: Locked g_shared(g_arena), then Account(g_shared, ...) in every task.
Locked* make_shared_allocator() {
  static Locked shared(g_arena);
  return &shared;
}

}  // namespace board_example
