#include "gtest/gtest.h"
#include "umbra/base/base.h"

namespace umbra::base::detail {
namespace {

TEST(SkeletonLinkProbeTest, ReturnsZero) { EXPECT_EQ(SkeletonLinkProbe(), 0); }

}
}
