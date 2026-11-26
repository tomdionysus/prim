//
// prim - Storage Primitives
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// SPDX-License-Identifier: MIT
//

// The guide's modules, objects and pools (docs/guide.md shows each by line
// range), on one arena. The host build compiles and runs it: prim_modules.

#include <cstdint>

#include "prim/alloc.h"
#include "prim/arena.h"
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/pool.h"
#include "prim/siphash.h"
#include "prim/timer.h"
#include "prim/trie.h"

alignas(8) static uint8_t ram[64 * 1024];
prim::Arena g_arena;

// One account per module.
struct Route {
  uint32_t next_hop;
};

struct Routing {
  prim::Account mem{g_arena, prim::Mem::Fast};  // looked up on every frame: fast
  prim::Pool nodes{mem, prim::Trie<Route>::kNodeSize, 32};
  prim::Trie<Route> table{nodes};  // its nodes from the pool
};

// Objects and containers.
struct ById;
struct Expiry;
struct Peer : prim::ListHook<>, prim::HashHook<ById>, prim::TimerHook<Expiry> {
  uint32_t id;
};

struct PeerById {
  using Key = uint32_t;
  static Key key(const Peer& p) { return p.id; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

struct Peers {
  prim::Account mem{g_arena, prim::Mem::Fast};
  prim::Hash<Peer, PeerById, ById> by_id{mem};  // only its bucket array is allocated
  prim::List<Peer> recent;                      // most recently heard first
};

// Objects made and freed all the time.
struct Frame {
  uint8_t len;
  uint8_t body[255];
};

prim::Account radio_mem(g_arena, prim::Mem::Fast);
prim::Pool frames(radio_mem, sizeof(Frame), 16);

void on_receive(const uint8_t* data, uint8_t len) {
  auto* f = static_cast<Frame*>(frames.get());
  if (!f) return;  // out of memory: drop it
  f->len = len;
  for (uint8_t i = 0; i < len; ++i) f->body[i] = data[i];
  // ... queue it; later, frames.put(f)
}

int main() {
  g_arena.add(ram, sizeof ram, prim::Mem::Fast);
  bool ok = true;
  {
    Routing routing;
    Route r{7};
    ok = ok && routing.table.insert(0x0A00000000000000ull, 8, r);
    ok = ok && routing.table.match(0x0A01020300000000ull) == &r;

    Peers peers;
    Peer p;
    p.id = 42;
    ok = ok && peers.by_id.insert(p);
    peers.recent.push_front(p);
    ok = ok && peers.by_id.find(42) == &p && peers.recent.front() == &p;
    peers.by_id.remove(p);
    prim::List<Peer>::remove(p);
  }
  const uint8_t data[3] = {1, 2, 3};
  on_receive(data, sizeof data);
  ok = ok && frames.in_use() == 1 && radio_mem.objects() == 1;
  return ok && g_arena.check() == 0 ? 0 : 1;
}
