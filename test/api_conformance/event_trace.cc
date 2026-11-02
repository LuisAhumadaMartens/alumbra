#include "event_trace.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace umbra::player::conformance {
namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::string_view Trim(std::string_view text) {
  while (!text.empty() && IsSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && IsSpace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::string Render(const std::vector<std::string>& names) {
  return absl::StrCat("[", absl::StrJoin(names, " "), "]");
}

class TraceBuilder {
 public:
  bool Apply(std::string_view key, std::string_view value, size_t line,
             std::string& error) {
    if (key == "scenario") {
      trace_.name = std::string(value);
    } else if (key == "provenance") {
      trace_.provenance = std::string(value);
    } else if (key == "ignore") {
      trace_.ignored = SplitWords(value);
    } else if (key == "step") {
      return AddStep(value, line, error);
    } else if (key == "expect") {
      return AddExpectation(value, line, error);
    } else {
      error = absl::StrCat("line ", line, ": unknown key '", key, "'");
      return false;
    }
    return true;
  }

  bool Finish(EventTrace& trace, std::string& error) {
    if (awaiting_expect_) {
      error = "the last step has no expect line";
      return false;
    }
    if (trace_.name.empty() || trace_.provenance.empty() ||
        trace_.steps.empty()) {
      error = "a trace needs scenario, provenance and at least one step";
      return false;
    }
    trace = std::move(trace_);
    return true;
  }

 private:
  bool AddStep(std::string_view value, size_t line, std::string& error) {
    if (awaiting_expect_) {
      error = absl::StrCat("line ", line,
                           ": a step needs an expect line before the next");
      return false;
    }
    std::vector<std::string> words = SplitWords(value);
    if (words.empty()) {
      error = absl::StrCat("line ", line, ": empty step");
      return false;
    }
    TraceStep step;
    step.action = words.front();
    step.arguments.assign(words.begin() + 1, words.end());
    trace_.steps.push_back(std::move(step));
    awaiting_expect_ = true;
    return true;
  }

  bool AddExpectation(std::string_view value, size_t line, std::string& error) {
    if (!awaiting_expect_) {
      error = absl::StrCat("line ", line, ": expect without a preceding step");
      return false;
    }
    trace_.steps.back().expected = SplitWords(value);
    awaiting_expect_ = false;
    return true;
  }

  EventTrace trace_;
  bool awaiting_expect_ = false;
};

}

std::vector<std::string> SplitWords(std::string_view text) {
  std::vector<std::string> words;
  std::string current;
  for (const char c : text) {
    if (IsSpace(c)) {
      if (!current.empty()) {
        words.push_back(std::move(current));
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    words.push_back(std::move(current));
  }
  return words;
}

bool ParseEventTrace(std::string_view text, EventTrace& trace,
                     std::string& error) {
  TraceBuilder builder;
  size_t line_number = 0;
  while (!text.empty()) {
    const size_t newline = text.find('\n');
    const std::string_view line = Trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view()
                                             : text.substr(newline + 1);
    ++line_number;
    if (line.empty()) {
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      error = absl::StrCat("line ", line_number, ": expected 'key: value'");
      return false;
    }
    if (!builder.Apply(Trim(line.substr(0, colon)),
                       Trim(line.substr(colon + 1)), line_number, error)) {
      return false;
    }
  }
  return builder.Finish(trace, error);
}

std::string CompareEventSequences(const std::vector<std::string>& expected,
                                  const std::vector<std::string>& actual) {
  if (expected == actual) {
    return {};
  }
  size_t index = 0;
  while (index < expected.size() && index < actual.size() &&
         expected[index] == actual[index]) {
    ++index;
  }
  return absl::StrCat("expected ", Render(expected), " but observed ",
                      Render(actual), "; first difference at position ", index);
}

}
