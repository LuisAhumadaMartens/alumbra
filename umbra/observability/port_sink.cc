#include "umbra/observability/port_sink.h"

#include <atomic>
#include <string>
#include <type_traits>
#include <variant>

#include "penumbra/interface/port_types.h"
#include "umbra/observability/category.h"
#include "umbra/observability/log.h"

namespace umbra::observability {
namespace {

penumbra::LogLevel ToPortLevel(Level level) {
  switch (level) {
    case Level::kVerbose:
      return penumbra::LogLevel::kVerbose;
    case Level::kDebug:
      return penumbra::LogLevel::kDebug;
    case Level::kInfo:
      return penumbra::LogLevel::kInfo;
    case Level::kWarning:
      return penumbra::LogLevel::kWarning;
    case Level::kError:
      return penumbra::LogLevel::kError;
  }
  return penumbra::LogLevel::kError;
}

void AppendValue(const FieldValue& value, std::string& text) {
  std::visit(
      [&text](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
          text += item ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
          const bool quote =
              item.empty() || item.find_first_of(" \t\"=") != std::string::npos;
          if (quote) {
            text += '"';
            for (const char ch : item) {
              if (ch == '"' || ch == '\\') {
                text += '\\';
              }
              text += ch;
            }
            text += '"';
          } else {
            text += item;
          }
        } else {
          text += std::to_string(item);
        }
      },
      value);
}

}

std::string FormatRecord(const LogRecord& record) {
  std::string text = "[";
  text += CategoryName(record.category);
  text += "] ";
  text += record.message;
  for (const LogField& field : record.fields) {
    text += ' ';
    text += field.name;
    text += '=';
    AppendValue(field.value, text);
  }
  return text;
}

void PortSink::Write(const LogRecord& record) {
  if (!port_->Log(ToPortLevel(record.level), FormatRecord(record)).ok()) {
    rejected_.fetch_add(1, std::memory_order_relaxed);
  }
}

}
