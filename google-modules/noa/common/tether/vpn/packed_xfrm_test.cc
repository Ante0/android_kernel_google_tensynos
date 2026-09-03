#include "vpn/packed_xfrm.h"

#include <cstring>
#include <type_traits>

#include "gtest/gtest.h"

namespace noa::module::vpn {
namespace {

TEST(PackedXfrmTest, WellFormedStructDefinitions) {
  EXPECT_GT(sizeof(packed_xfrm_state), 0u);
  EXPECT_GT(sizeof(packed_xfrm_policy), 0u);
}

TEST(PackedXfrmTest, CopyFromXfrmState) {
  constexpr bool kIsSameType = std::is_same_v<xfrm_state, packed_xfrm_state>;
  ASSERT_TRUE(kIsSameType);

  xfrm_state state = {};
  state.id.spi = 12;
  state.sel.saddr.a4 = 34;

  packed_xfrm_state packed_state;
  copy_from_xfrm_state(&packed_state, &state);

  EXPECT_EQ(0, std::memcmp(&state, &packed_state, sizeof(packed_state)));
}

TEST(PackedXfrmTest, CopyFromXfrmPolicy) {
  constexpr bool kIsSameType = std::is_same_v<xfrm_policy, packed_xfrm_policy>;
  ASSERT_TRUE(kIsSameType);

  xfrm_policy policy = {};
  policy.if_id = 56;
  policy.selector.saddr.a4 = 78;

  packed_xfrm_policy packed_policy;
  copy_from_xfrm_policy(&packed_policy, &policy);

  EXPECT_EQ(0, std::memcmp(&policy, &packed_policy, sizeof(packed_policy)));
}

}  // namespace
}  // namespace noa::module::vpn
