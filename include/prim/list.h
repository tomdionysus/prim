#pragma once

// Intrusive doubly linked list. The link lives in the object, which derives
// from ListHook<Tag> once for each list it can be on; the list allocates
// nothing and is two pointers. Circular around the list's own head, so no
// operation tests for an end. A link not on a list is null.

#include <cstdint>

namespace prim {

struct Link {
  Link* next = nullptr;
  Link* prev = nullptr;
};

template <class Tag = void>
struct ListHook : Link {};

template <class T, class Tag = void>
class List {
 public:
  List() = default;
  List(const List&) = delete;
  List& operator=(const List&) = delete;

  bool empty() const { return head_.next == &head_; }
  T* front() const { return at(head_.next); }
  T* back() const { return at(head_.prev); }
  T* next(const T& x) const { return at(hook(x)->next); }  // nullptr after the back
  T* prev(const T& x) const { return at(hook(x)->prev); }  // nullptr before the front

  void push_front(T& x) { join(&head_, hook(x)); }
  void push_back(T& x) { join(head_.prev, hook(x)); }
  void insert_after(T& at, T& x) { join(hook(at), hook(x)); }
  void move_front(T& x) {
    remove(x);
    push_front(x);
  }
  T* pop_front() {
    T* x = front();
    if (x) remove(*x);
    return x;
  }

  static bool linked(const T& x) { return hook(x)->next != nullptr; }
  static void remove(T& x) {
    Link* l = hook(x);
    l->prev->next = l->next;
    l->next->prev = l->prev;
    l->next = l->prev = nullptr;
  }

  uint32_t size() const {
    uint32_t n = 0;
    for (const Link* l = head_.next; l != &head_; l = l->next) ++n;
    return n;
  }

 private:
  // Reference casts: neither side can be null, so no null test is compiled in.
  static Link* hook(const T& x) { return &const_cast<ListHook<Tag>&>(static_cast<const ListHook<Tag>&>(x)); }
  T* at(Link* l) const { return l == &head_ ? nullptr : &static_cast<T&>(static_cast<ListHook<Tag>&>(*l)); }
  static void join(Link* after, Link* l) {
    l->prev = after;
    l->next = after->next;
    after->next->prev = l;
    after->next = l;
  }

  Link head_{&head_, &head_};
};

}  // namespace prim
