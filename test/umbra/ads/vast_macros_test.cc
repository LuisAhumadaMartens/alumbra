#include "umbra/ads/vast_macros.h"

#include "gtest/gtest.h"

namespace umbra::ads {
namespace {

TEST(VastMacrosTest, ExpandsKnownValuesAndPreservesUnknownTokens) {
  const VastMacroValues values = {{"ERRORCODE", "403"},
                                  {"CACHEBUSTING", "0012345678"}};
  EXPECT_EQ(
      ExpandVastMacros("https://track.test/?code=[ERRORCODE]&cb=[CACHEBUSTING]"
                       "&unknown=[UNLISTED]",
                       values),
      "https://track.test/?code=403&cb=0012345678&unknown=[UNLISTED]");
}

TEST(VastMacrosTest, LeavesIncompleteTokensAndRepeatedMarkersSafe) {
  const VastMacroValues values = {{"ERRORCODE", "403"}};
  EXPECT_EQ(ExpandVastMacros("x[ERRORCODE]y[ERRORCODE]z[", values),
            "x403y403z[");
}

}
}
