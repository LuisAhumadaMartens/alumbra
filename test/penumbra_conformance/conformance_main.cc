#include "gtest/gtest.h"
#include "penumbra_conformance/report.h"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  penumbra::conformance::InstallReportFromArguments(argc, argv);
  return RUN_ALL_TESTS();
}
