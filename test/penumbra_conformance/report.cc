#include "penumbra_conformance/report.h"

#include <cstddef>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "penumbra/interface/interface.h"

namespace penumbra::conformance {
namespace {

constexpr std::string_view kReportFlag = "--conformance_report=";
constexpr int kReportSchema = 1;

std::string Escape(const std::string& text) {
  std::string result;
  for (const char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      default:
        result += c;
    }
  }
  return result;
}

std::string Quote(const std::string& text) {
  return "\"" + Escape(text) + "\"";
}

std::string ResultName(const ::testing::TestResult& result) {
  if (result.Skipped()) {
    return "skipped";
  }
  return result.Failed() ? "failed" : "passed";
}

}

std::string FormatReport(const std::vector<ReportEntry>& entries,
                         int interface_version) {
  std::map<std::string, std::vector<const ReportEntry*>> by_port;
  for (const auto& entry : entries) {
    by_port[entry.port].push_back(&entry);
  }
  std::string text =
      "{\n  \"schema\": " + std::to_string(kReportSchema) +
      ",\n  \"interface_version\": " + std::to_string(interface_version) +
      ",\n  \"ports\": [";
  bool first_port = true;
  for (const auto& [port, port_entries] : by_port) {
    text += first_port ? "\n" : ",\n";
    first_port = false;
    text +=
        "    {\n      \"name\": " + Quote(port) +
        ",\n      \"interface_version\": " + std::to_string(interface_version) +
        ",\n      \"tests\": [";
    bool first_test = true;
    for (const auto* entry : port_entries) {
      text += first_test ? "\n" : ",\n";
      first_test = false;
      text += "        {\"suite\": " + Quote(entry->suite) +
              ", \"test\": " + Quote(entry->test) +
              ", \"result\": " + Quote(entry->result) + "}";
    }
    text += "\n      ]\n    }";
  }
  text += "\n  ]\n}\n";
  return text;
}

ReportListener::ReportListener(std::string path) : path_(std::move(path)) {}

void ReportListener::OnTestEnd(const ::testing::TestInfo& info) {
  const std::string name = info.name();
  const auto slash = name.rfind('/');
  if (info.value_param() == nullptr || slash == std::string::npos) {
    return;
  }
  std::string suite = info.test_suite_name();
  const auto suite_slash = suite.find('/');
  if (suite_slash != std::string::npos) {
    suite = suite.substr(suite_slash + 1);
  }
  entries_.push_back({.port = name.substr(slash + 1),
                      .suite = std::move(suite),
                      .test = name.substr(0, slash),
                      .result = ResultName(*info.result())});
}

void ReportListener::OnTestProgramEnd(
    [[maybe_unused]] const ::testing::UnitTest& unit_test) {
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  out << FormatReport(entries_, kApiVersion);
}

void InstallReportFromArguments(int argc, char** argv) {
  std::string path;
  for (const std::string_view argument :
       std::span<char*>(argv, static_cast<size_t>(argc))) {
    if (argument.starts_with(kReportFlag)) {
      path = std::string(argument.substr(kReportFlag.size()));
    }
  }
  if (path.empty()) {
    return;
  }
  auto listener = std::make_unique<ReportListener>(std::move(path));
  ::testing::UnitTest::GetInstance()->listeners().Append(listener.release());
}

}
