#ifndef TEST_PENUMBRA_CONFORMANCE_REPORT_H_
#define TEST_PENUMBRA_CONFORMANCE_REPORT_H_

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace penumbra::conformance {

struct ReportEntry {
  std::string port;
  std::string suite;
  std::string test;
  std::string result;
};

std::string FormatReport(const std::vector<ReportEntry>& entries,
                         int interface_version);

class ReportListener final : public ::testing::EmptyTestEventListener {
 public:
  explicit ReportListener(std::string path);
  void OnTestEnd(const ::testing::TestInfo& info) override;
  void OnTestProgramEnd(const ::testing::UnitTest& unit_test) override;

 private:
  std::string path_;
  std::vector<ReportEntry> entries_;
};

void InstallReportFromArguments(int argc, char** argv);

}

#endif
