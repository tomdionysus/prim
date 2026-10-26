#pragma once

// One queue of deadlines, on the red-black tree: a timer is a tree node and
// its deadline. due() and run() take only what is due instead of scanning
// every table, and next() says how long the caller may sleep. Equal
// deadlines come out in the order they were armed. The owner derives from
// TimerHook<Tag> once for each timer it carries.
//
// run(now) first takes every timer due by now out of the tree, then fires
// them in order. A fire that arms a timer again for now or earlier puts it
// back in the tree, for the next run: a timer cannot keep run() from
// returning.

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

  void arm(Timer& t, uint64_t at);  // re-arms a timer already armed, or taken out by run() and not yet fired
  void cancel(Timer& t);            // a timer not armed is left alone
  static bool armed(const Timer& t) { return rb_linked(&t); }
  uint64_t next() const;            // the earliest deadline, or kNever
  Timer* due(uint64_t now);         // the earliest timer due by now, disarmed; else nullptr
  // Every timer due by now, earliest first, each disarmed then fired (one
  // without fire is only disarmed); a fire may arm or cancel any timer, and
  // may call run() itself. Returns how many this call fired.
  uint32_t run(uint64_t now);

 private:
  // A timer run() has taken out of the tree is on a list through its two
  // child links, and marked by its parent word: itself, with the low bit set.
  static bool pending(const Timer& t) { return t.pc == (reinterpret_cast<uintptr_t>(&t) | 1); }
  void unpend(Timer& t);

  RbRoot t_;
  Timer* pend_ = nullptr;  // taken out by run() and not yet fired, earliest first
};

// ---- Implementation ----

namespace detail {
inline Timer* first_timer(const RbRoot& t) { return static_cast<Timer*>(rb_end(t, 0)); }
}  // namespace detail

inline void TimerQueue::unpend(Timer& t) {
  RbNode* p = t.c[0];
  RbNode* n = t.c[1];
  if (p) {
    p->c[1] = n;
  } else {
    pend_ = static_cast<Timer*>(n);
  }
  if (n) n->c[0] = p;
  t.pc = reinterpret_cast<uintptr_t>(&t);
  t.c[0] = t.c[1] = nullptr;
}

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
  if (pending(t)) {
    unpend(t);
  } else if (armed(t)) {
    rb_erase(t_, &t);
  }
}

inline uint64_t TimerQueue::next() const {
  const Timer* t = detail::first_timer(t_);
  uint64_t at = t ? t->at : kNever;
  if (pend_ && pend_->at < at) at = pend_->at;
  return at;
}

inline Timer* TimerQueue::due(uint64_t now) {
  Timer* t = detail::first_timer(t_);
  if (pend_ && pend_->at <= now && (!t || pend_->at <= t->at)) {
    t = pend_;
    unpend(*t);
    return t;
  }
  if (!t || t->at > now) return nullptr;
  rb_erase(t_, t);
  return t;
}

inline uint32_t TimerQueue::run(uint64_t now) {
  // Everything due by now onto the end of the pending list, in order.
  RbNode* tail = pend_;
  while (tail && tail->c[1]) tail = tail->c[1];
  for (Timer* t; (t = detail::first_timer(t_)) && t->at <= now;) {
    rb_erase(t_, t);
    t->pc = reinterpret_cast<uintptr_t>(t) | 1;
    t->c[0] = tail;
    t->c[1] = nullptr;
    if (tail) {
      tail->c[1] = t;
    } else {
      pend_ = t;
    }
    tail = t;
  }
  // Then fired. A fire that arms anything puts it in the tree, not here.
  uint32_t n = 0;
  for (Timer* t; (t = pend_); ++n) {
    unpend(*t);
    if (t->fire) t->fire(*t);
  }
  return n;
}

}  // namespace prim
