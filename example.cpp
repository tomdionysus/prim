#include <cstdint>
#include <cstring>

#include "prim/alloc.h"
#include "prim/arena.h"
#include "prim/hash.h"
#include "prim/list.h"
#include "prim/siphash.h"
#include "prim/timer.h"

// A session: on a list of sessions, in a table by peer address, with a
// timer. Each container's link is inside the object, through a hook.
struct ByAddr;   // tags, so one object can be in several containers
struct Expiry;
struct Session : prim::ListHook<>, prim::HashHook<ByAddr>, prim::TimerHook<Expiry> {
  uint32_t addr;
  uint32_t packets = 0;
};

// What the table needs to know about its objects.
struct SessionByAddr {
  using Key = uint32_t;
  static Key key(const Session& s) { return s.addr; }
  static uint32_t hash(const Key& k) { return prim::hash32(&k, sizeof k); }
  static bool eq(const Key& a, const Key& b) { return a == b; }
};

alignas(8) static uint8_t fast_ram[16 * 1024];

int main() {
  // One arena for the whole program, over memory it is given: internal RAM
  // as fast, PSRAM (on a board with any) as bulk.
  prim::Arena arena;
  arena.add(fast_ram, sizeof fast_ram, prim::Mem::Fast);

  // A module allocates through its Account: everything it makes is of its
  // kind of memory and counted to it.
  prim::Account sessions_mem(arena, prim::Mem::Fast);

  prim::List<Session> all;                                 // allocates nothing
  prim::Hash<Session, SessionByAddr, ByAddr> by_addr(sessions_mem);  // buckets from the account
  prim::TimerQueue timers;

  Session s;
  s.addr = 0x0A000001;
  s.fire = [](prim::Timer& t) {
    Session& self = static_cast<Session&>(static_cast<prim::TimerHook<Expiry>&>(t));
    self.packets = 0;  // expired
  };

  all.push_back(s);
  by_addr.insert(s);          // false only if there are no buckets at all
  timers.arm(s, 1000);        // deadline in the caller's units

  if (Session* found = by_addr.find(0x0A000001)) ++found->packets;
  timers.run(1000);           // fires s, which is now disarmed
  by_addr.remove(s);
  prim::List<Session>::remove(s);

  return s.packets == 0 && arena.check() == 0 && sessions_mem.objects() == 1 ? 0 : 1;
}
