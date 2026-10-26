#pragma once

// Insertion sort, for the small arrays an application sorts: a listing of a few
// dozen, or an array of pointers put in order for display. Stable, in
// place, nothing allocated, O(n) when the array is nearly sorted and O(n^2)
// at worst, so a few dozen elements at most. less(a, b) says a goes before b.

namespace prim {

template <class T, class Less>
void sort(T* begin, T* end, Less less) {
  for (T* i = begin + 1; i < end; ++i) {
    T x = *i;
    T* j = i;
    for (; j > begin && less(x, j[-1]); --j) *j = j[-1];
    *j = x;
  }
}

}  // namespace prim
