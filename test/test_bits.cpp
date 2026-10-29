#include "check.h"
#include "prim/bits.h"

// Leading zeros: the portable version against the one the build uses, for
// every bit position, every position with every lower bit set, and a seeded
// run of random words.

namespace {

TEST(bits_clz_every_position) {
  for (int i = 0; i < 32; ++i) {
    const uint32_t one = uint32_t(1) << i;
    CHECK_EQ(prim::clz32(one), 31 - i);
    CHECK_EQ(prim::clz32_portable(one), 31 - i);
    CHECK_EQ(prim::clz32_portable(one | (one - 1)), 31 - i);
    CHECK_EQ(prim::clz32_portable(one | (one >> 1)), 31 - i);
  }
  for (int i = 0; i < 64; ++i) {
    const uint64_t one = uint64_t(1) << i;
    CHECK_EQ(prim::clz64(one), 63 - i);
    CHECK_EQ(prim::clz64_portable(one), 63 - i);
    CHECK_EQ(prim::clz64_portable(one | (one - 1)), 63 - i);
  }
  CHECK_EQ(prim::clz32_portable(~uint32_t(0)), 0);
  CHECK_EQ(prim::clz64_portable(~uint64_t(0)), 0);
}

TEST(bits_clz_random) {
  check::Rng rng(0xB175);
  for (int n = 0; n < 100000; ++n) {
    const uint64_t x = (uint64_t(rng()) << 32 | rng()) >> rng.below(64);  // every length of word
    if (uint32_t(x)) CHECK_EQ(prim::clz32_portable(uint32_t(x)), prim::clz32(uint32_t(x)));
    if (x) CHECK_EQ(prim::clz64_portable(x), prim::clz64(x));
  }
}

}  // namespace
