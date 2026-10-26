#pragma once

// One queue of deadlines, on the red-black tree: a timer is a tree node and
// its deadline. due() and run() take only what is due instead of scanning every
// table, and next() says how long the caller may sleep. Equal deadlines come
// out in the order they were armed. The owner derives from TimerHook<Tag>
// once for each timer it carries.

#include <cstdint>

#include "prim/rbtree.h"

namespace prim {

struct Timer : RbNode {
  void (*fire)(Timer&) = nullptr;  // what run() calls; here it fills the node's padding on the S3
  uint64_t at = 0;
};

template <class Tag = void>
struct TimerHook : Timer {};

class TimerQueue {
 public:
  static constexpr uint64_t kNever = ~uint64_t(0);

  void arm(Timer& t, uint64_t at);  // re-arms a timer already armed
  void cancel(Timer& t);            // a timer not armed is left alone
  static bool armed(const Timer& t) { return rb_linked(&t); }
  uint64_t next() const;            // the earliest deadline, or kNever
  Timer* due(uint64_t now);         // the earliest timer due by now, disarmed; else nullptr
  // Every timer due by now, earliest first, each disarmed then fired (one
  // without fire is only disarmed); a fire may arm or cancel any timer.
  // Returns how many.
  uint32_t run(uint64_t now);

 private:
  RbRoot t_;
};

// ---- Implementation ----

namespace detail {
inline Timer* first_timer(const RbRoot& t) { return static_cast<Timer*>(rb_end(t, 0)); }
}  // namespace detail

inline void TimerQueue::arm(Timer& t, uint64_t at) {
  cancel(t);
  t.at = at;
  RbNode* p = nullptr;
  int side = 0;
  for (RbNode* n = t_.root; n; n = n->c[side]) {
    p = n;
    side = at >= static_cast<Timer*>(n)->at;
  }
  rb_insert(t_, &t, p, side);
}

inline void TimerQueue::cancel(Timer& t) {
  if (armed(t)) rb_erase(t_, &t);
}

inline uint64_t TimerQueue::next() const {
  const Timer* t = detail::first_timer(t_);
  return t ? t->at : kNever;
}

inline Timer* TimerQueue::due(uint64_t now) {
  Timer* t = detail::first_timer(t_);
  if (!t || t->at > now) return nullptr;
  rb_erase(t_, t);
  return t;
}

inline uint32_t TimerQueue::run(uint64_t now) {
  uint32_t n = 0;
  for (Timer* t; (t = due(now)); ++n) {
    if (t->fire) t->fire(*t);
  }
  return n;
}

}  // namespace prim
