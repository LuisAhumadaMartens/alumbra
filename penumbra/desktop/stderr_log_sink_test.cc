#include "penumbra/desktop/stderr_log_sink.h"

#include <ios>
#include <sstream>

#include "gtest/gtest.h"
#include "penumbra/interface/port_types.h"

namespace penumbra::desktop {
namespace {

TEST(StderrLogSinkTest, WritesOneTaggedLinePerMessage) {
  std::ostringstream stream;
  StderrLogSink sink(stream);
  EXPECT_TRUE(sink.Log(LogLevel::kInfo, "[net] fetched segment_count=3").ok());
  EXPECT_TRUE(sink.Log(LogLevel::kError, "[drm] failed").ok());
  EXPECT_EQ(stream.str(), "I [net] fetched segment_count=3\nE [drm] failed\n");
}

TEST(StderrLogSinkTest, ReportsWriteFailureAsNonOk) {
  std::ostringstream stream;
  stream.setstate(std::ios::badbit);
  StderrLogSink sink(stream);
  EXPECT_FALSE(sink.Log(LogLevel::kInfo, "x").ok());
}

}
}
