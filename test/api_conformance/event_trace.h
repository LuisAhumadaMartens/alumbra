#ifndef TEST_API_CONFORMANCE_EVENT_TRACE_H_
#define TEST_API_CONFORMANCE_EVENT_TRACE_H_

#include <string>
#include <string_view>
#include <vector>

namespace umbra::player::conformance {

struct TraceStep {
  std::string action;
  std::vector<std::string> arguments;
  std::vector<std::string> expected;
};

struct EventTrace {
  std::string name;
  std::string provenance;
  std::vector<std::string> ignored;
  std::vector<TraceStep> steps;
};

bool ParseEventTrace(std::string_view text, EventTrace& trace,
                     std::string& error);

std::string CompareEventSequences(const std::vector<std::string>& expected,
                                  const std::vector<std::string>& actual);

std::vector<std::string> SplitWords(std::string_view text);

}

#endif
