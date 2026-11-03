#include "umbra/player/srt_parser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace umbra::player::internal {
namespace {

constexpr size_t kMaximumInputBytes = size_t{16} * 1024U * 1024U;

void AppendUtf8(uint32_t code_point, std::string* output) {
  if (code_point <= 0x7fU) {
    output->push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7ffU) {
    output->push_back(static_cast<char>(0xc0U | (code_point >> 6U)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else if (code_point <= 0xffffU) {
    output->push_back(static_cast<char>(0xe0U | (code_point >> 12U)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  } else {
    output->push_back(static_cast<char>(0xf0U | (code_point >> 18U)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3fU)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3fU)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3fU)));
  }
}

bool IsValidUtf8(std::string_view input) {
  size_t index = 0;
  while (index < input.size()) {
    const auto first = static_cast<uint8_t>(input[index]);
    if (first <= 0x7fU) {
      ++index;
      continue;
    }
    size_t length = 0;
    uint32_t code_point = 0;
    if (first >= 0xc2U && first <= 0xdfU) {
      length = 2;
      code_point = first & 0x1fU;
    } else if (first >= 0xe0U && first <= 0xefU) {
      length = 3;
      code_point = first & 0x0fU;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      length = 4;
      code_point = first & 0x07U;
    } else {
      return false;
    }
    if (length > input.size() - index) {
      return false;
    }
    for (size_t offset = 1; offset < length; ++offset) {
      const auto next = static_cast<uint8_t>(input[index + offset]);
      if ((next & 0xc0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3fU);
    }
    if ((length == 3 && code_point < 0x800U) ||
        (length == 4 && code_point < 0x10000U) || code_point > 0x10ffffU ||
        (code_point >= 0xd800U && code_point <= 0xdfffU)) {
      return false;
    }
    index += length;
  }
  return true;
}

uint16_t Read16(std::string_view bytes, size_t offset, bool little_endian) {
  const auto first = static_cast<uint8_t>(bytes[offset]);
  const auto second = static_cast<uint8_t>(bytes[offset + 1]);
  return little_endian
             ? static_cast<uint16_t>(first |
                                     (static_cast<uint16_t>(second) << 8U))
             : static_cast<uint16_t>((static_cast<uint16_t>(first) << 8U) |
                                     second);
}

bool DecodeUtf16(std::string_view bytes, bool little_endian,
                 std::string* output) {
  if (bytes.size() % 2U != 0U) {
    return false;
  }
  size_t index = 0;
  while (index < bytes.size()) {
    uint32_t code_point = Read16(bytes, index, little_endian);
    index += 2;
    if (code_point >= 0xd800U && code_point <= 0xdbffU) {
      if (bytes.size() - index < 2U) {
        return false;
      }
      const uint32_t low = Read16(bytes, index, little_endian);
      if (low < 0xdc00U || low > 0xdfffU) {
        return false;
      }
      index += 2;
      code_point = 0x10000U + ((code_point - 0xd800U) << 10U) + (low - 0xdc00U);
    } else if (code_point >= 0xdc00U && code_point <= 0xdfffU) {
      return false;
    }
    AppendUtf8(code_point, output);
  }
  return true;
}

std::string DecodeWindows1252(std::string_view bytes) {
  constexpr std::array<uint16_t, 32> kWindows1252 = {
      0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
      0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
      0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
      0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};
  std::string output;
  output.reserve(bytes.size() * 2U);
  for (const char byte : bytes) {
    const auto value = static_cast<uint8_t>(byte);
    const uint32_t code_point =
        value >= 0x80U && value <= 0x9fU
            ? kWindows1252.at(static_cast<size_t>(value - 0x80U))
            : value;
    AppendUtf8(code_point, &output);
  }
  return output;
}

std::string CanonicalEncoding(std::string_view encoding) {
  std::string canonical;
  canonical.reserve(encoding.size());
  for (const unsigned char character : encoding) {
    canonical.push_back(
        character == '_' ? '-' : static_cast<char>(std::tolower(character)));
  }
  return canonical;
}

double DecodeConfidence(const SrtParseOptions& options) {
  if (!options.encoding_override.empty() || !options.auto_detect) {
    return 1.0;
  }
  return 0.0;
}

bool DecodeConfigured(std::string_view input, std::string_view requested,
                      const SrtParseOptions& options, std::string* output,
                      std::string* encoding, double* confidence) {
  *confidence = DecodeConfidence(options);
  if (requested == "utf-8" || requested == "utf8") {
    *encoding = "utf-8";
    output->assign(input);
    return IsValidUtf8(*output);
  }
  if (requested == "utf-16le" || requested == "utf-16be") {
    *encoding = requested;
    return DecodeUtf16(input, requested == "utf-16le", output);
  }
  if (requested == "windows-1252" || requested == "windows1252" ||
      requested == "cp1252") {
    *encoding = "windows-1252";
    *output = DecodeWindows1252(input);
    return true;
  }
  if (requested == "iso-8859-1" || requested == "latin1" ||
      requested == "latin-1") {
    *encoding = "iso-8859-1";
    output->clear();
    output->reserve(input.size() * 2U);
    for (const char byte : input) {
      AppendUtf8(static_cast<uint8_t>(byte), output);
    }
    return true;
  }
  return false;
}

bool Decode(std::string_view input, const SrtParseOptions& options,
            std::string* output, std::string* encoding, double* confidence) {
  if (input.size() >= 3U && static_cast<uint8_t>(input[0]) == 0xefU &&
      static_cast<uint8_t>(input[1]) == 0xbbU &&
      static_cast<uint8_t>(input[2]) == 0xbfU) {
    *encoding = "utf-8";
    *confidence = 1.0;
    output->assign(input.substr(3));
    return IsValidUtf8(*output);
  }
  if (input.size() >= 2U && static_cast<uint8_t>(input[0]) == 0xffU &&
      static_cast<uint8_t>(input[1]) == 0xfeU) {
    *encoding = "utf-16le";
    *confidence = 1.0;
    return DecodeUtf16(input.substr(2), true, output);
  }
  if (input.size() >= 2U && static_cast<uint8_t>(input[0]) == 0xfeU &&
      static_cast<uint8_t>(input[1]) == 0xffU) {
    *encoding = "utf-16be";
    *confidence = 1.0;
    return DecodeUtf16(input.substr(2), false, output);
  }
  const bool overridden = !options.encoding_override.empty();
  if (!overridden && IsValidUtf8(input)) {
    *encoding = "utf-8";
    *confidence = 1.0;
    output->assign(input);
    return true;
  }
  const std::string_view selected =
      overridden ? options.encoding_override : options.fallback_encoding;
  const std::string requested = CanonicalEncoding(selected);
  return DecodeConfigured(input, requested, options, output, encoding,
                          confidence);
}

bool ParseInteger(std::string_view text, int base, int64_t* result) {
  if (text.empty()) {
    return false;
  }
  int64_t value = 0;
  for (const char character : text) {
    int digit = -1;
    if (character >= '0' && character <= '9') {
      digit = character - '0';
    } else if (base == 16 && character >= 'a' && character <= 'f') {
      digit = character - 'a' + 10;
    } else if (base == 16 && character >= 'A' && character <= 'F') {
      digit = character - 'A' + 10;
    }
    if (digit < 0 || digit >= base ||
        value > (std::numeric_limits<int64_t>::max() - digit) / base) {
      return false;
    }
    value = (value * base) + digit;
  }
  *result = value;
  return true;
}

bool ParseTimestamp(std::string_view text, double* seconds) {
  const size_t first_colon = text.find(':');
  if (first_colon == std::string_view::npos) {
    return false;
  }
  const size_t second_colon = text.find(':', first_colon + 1U);
  if (second_colon == std::string_view::npos) {
    return false;
  }
  const size_t separator = text.find_first_of(",.", second_colon + 1U);
  if (separator == std::string_view::npos ||
      text.size() - separator - 1U != 3U) {
    return false;
  }
  int64_t hours = 0;
  int64_t minutes = 0;
  int64_t whole_seconds = 0;
  int64_t milliseconds = 0;
  if (!ParseInteger(text.substr(0, first_colon), 10, &hours) ||
      !ParseInteger(
          text.substr(first_colon + 1U, second_colon - first_colon - 1U), 10,
          &minutes) ||
      !ParseInteger(
          text.substr(second_colon + 1U, separator - second_colon - 1U), 10,
          &whole_seconds) ||
      !ParseInteger(text.substr(separator + 1U), 10, &milliseconds) ||
      hours < 0 || hours > std::numeric_limits<int>::max() || minutes < 0 ||
      minutes > 59 || whole_seconds < 0 || whole_seconds > 59 ||
      milliseconds < 0 || milliseconds > 999) {
    return false;
  }
  *seconds = (static_cast<double>(hours) * 3600.0) +
             (static_cast<double>(minutes) * 60.0) +
             static_cast<double>(whole_seconds) +
             (static_cast<double>(milliseconds) / 1000.0);
  return true;
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

struct TextStyle {
  bool bold = false;
  bool italic = false;
  bool underline = false;
  std::string color;
};

void AddRun(SrtCue* cue, std::string text, const TextStyle& style) {
  if (text.empty()) {
    return;
  }
  if (!cue->runs.empty() && cue->runs.back().bold == style.bold &&
      cue->runs.back().italic == style.italic &&
      cue->runs.back().underline == style.underline &&
      cue->runs.back().color == style.color) {
    cue->runs.back().text += text;
    return;
  }
  SrtStyledTextRun run;
  run.text = std::move(text);
  run.bold = style.bold;
  run.italic = style.italic;
  run.underline = style.underline;
  run.color = style.color;
  cue->runs.push_back(std::move(run));
}

std::string_view FontColour(std::string_view attributes) {
  const size_t colour_start = CanonicalEncoding(attributes).find("color=");
  if (colour_start == std::string_view::npos) {
    return {};
  }
  std::string_view value = Trim(attributes.substr(colour_start + 6U));
  if (value.empty()) {
    return {};
  }
  if (value.front() == '\'' || value.front() == '"') {
    const char quote = value.front();
    value.remove_prefix(1U);
    return value.substr(0, value.find(quote));
  }
  return value.substr(0, value.find_first_of(" \t"));
}

bool ConsumeTag(std::string_view line, size_t* position, TextStyle* style) {
  if (line[*position] != '<') {
    return false;
  }
  const size_t end = line.find('>', *position + 1U);
  if (end == std::string_view::npos) {
    return false;
  }
  std::string_view tag =
      Trim(line.substr(*position + 1U, end - *position - 1U));
  bool closing = false;
  if (!tag.empty() && tag.front() == '/') {
    closing = true;
    tag.remove_prefix(1U);
  }
  const size_t space = tag.find_first_of(" \t");
  const std::string_view attributes = space == std::string_view::npos
                                          ? std::string_view()
                                          : tag.substr(space + 1U);
  const std::string name = CanonicalEncoding(tag.substr(0, space));
  if (name == "i") {
    style->italic = !closing;
  } else if (name == "b") {
    style->bold = !closing;
  } else if (name == "u") {
    style->underline = !closing;
  } else if (name == "font") {
    style->color =
        closing ? std::string() : std::string(FontColour(attributes));
  }
  *position = end + 1U;
  return true;
}

bool DecodeEntity(std::string_view line, size_t* position,
                  std::string* output) {
  if (line[*position] != '&') {
    return false;
  }
  const size_t end = line.find(';', *position + 1U);
  if (end == std::string_view::npos || end - *position >= 12U) {
    return false;
  }
  const std::string_view entity =
      line.substr(*position + 1U, end - *position - 1U);
  uint32_t code_point = 0;
  if (entity == "amp") {
    code_point = '&';
  } else if (entity == "lt") {
    code_point = '<';
  } else if (entity == "gt") {
    code_point = '>';
  } else if (entity == "nbsp") {
    code_point = 0xa0U;
  } else if (entity == "quot") {
    code_point = '"';
  } else if (entity.size() > 1U && entity.front() == '#') {
    const bool hexadecimal = entity.size() > 2U && entity[1] == 'x';
    const std::string_view digits = entity.substr(hexadecimal ? 2U : 1U);
    int64_t parsed_value = 0;
    if (!ParseInteger(digits, hexadecimal ? 16 : 10, &parsed_value) ||
        parsed_value > 0x10ffff ||
        (parsed_value >= 0xd800 && parsed_value <= 0xdfff)) {
      return false;
    }
    code_point = static_cast<uint32_t>(parsed_value);
  } else {
    return false;
  }
  AppendUtf8(code_point, output);
  *position = end + 1U;
  return true;
}

void ParseTextLine(std::string_view line, SrtCue* cue, TextStyle* style) {
  size_t position = 0;
  std::string plain;
  while (position < line.size()) {
    const bool alignment =
        line.substr(position).starts_with("{\\an") &&
        position + 5U < line.size() && line[position + 4U] >= '1' &&
        line[position + 4U] <= '9' && line[position + 5U] == '}';
    if (alignment) {
      position += 6U;
    } else if (line[position] == '<') {
      AddRun(cue, std::move(plain), *style);
      plain.clear();
      if (ConsumeTag(line, &position, style)) {
        continue;
      }
      plain.push_back(line[position]);
      ++position;
    } else if (!DecodeEntity(line, &position, &plain)) {
      plain.push_back(line[position]);
      ++position;
    }
  }
  AddRun(cue, std::move(plain), *style);
}

void ParseAlignment(std::string_view line, SrtCue* cue) {
  for (size_t position = line.find("{\\an"); position != std::string_view::npos;
       position = line.find("{\\an", position + 1U)) {
    if (position + 5U >= line.size()) {
      continue;
    }
    const char alignment = line[position + 4U];
    if (alignment < '1' || alignment > '9' || line[position + 5U] != '}') {
      continue;
    }
    const int value = alignment - '0';
    const int row = (value - 1) / 3;
    const int column = (value - 1) % 3;
    if (row == 0) {
      cue->line = 100.0;
    } else if (row == 1) {
      cue->line = 50.0;
    } else {
      cue->line = 0.0;
    }
    if (column == 0) {
      cue->position = 0.0;
    } else if (column == 1) {
      cue->position = 50.0;
    } else {
      cue->position = 100.0;
    }
  }
}

std::vector<std::string_view> Lines(std::string_view text) {
  std::vector<std::string_view> lines;
  size_t start = 0;
  for (size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\r' && text[index] != '\n') {
      continue;
    }
    lines.push_back(text.substr(start, index - start));
    if (text[index] == '\r' && index + 1U < text.size() &&
        text[index + 1U] == '\n') {
      ++index;
    }
    start = index + 1U;
  }
  lines.push_back(text.substr(start));
  return lines;
}

bool ParseCueBlock(const std::vector<std::string_view>& lines, size_t start,
                   size_t end, uint64_t id, SrtCue* cue) {
  size_t timing_line = start;
  if (lines[timing_line].find("-->") == std::string_view::npos) {
    ++timing_line;
  }
  if (timing_line >= end) {
    return false;
  }
  const std::string_view timing = lines[timing_line];
  const size_t arrow = timing.find("-->");
  if (arrow == std::string_view::npos) {
    return false;
  }
  const std::string_view start_time = Trim(timing.substr(0, arrow));
  const std::string_view end_part = Trim(timing.substr(arrow + 3U));
  const size_t settings = end_part.find_first_of(" \t");
  const std::string_view end_time = end_part.substr(0, settings);
  if (!ParseTimestamp(start_time, &cue->start_time) ||
      !ParseTimestamp(end_time, &cue->end_time) ||
      cue->end_time < cue->start_time) {
    return false;
  }
  cue->id = id;
  TextStyle style;
  for (size_t index = timing_line + 1U; index < end; ++index) {
    ParseAlignment(lines[index], cue);
    ParseTextLine(lines[index], cue, &style);
    if (index + 1U < end) {
      AddRun(cue, "\n", style);
    }
  }
  return !cue->runs.empty();
}

std::vector<SrtCue> ParseCues(std::string_view text) {
  const std::vector<std::string_view> lines = Lines(text);
  std::vector<SrtCue> cues;
  size_t line = 0;
  uint64_t next_id = 1;
  while (line < lines.size()) {
    while (line < lines.size() && Trim(lines[line]).empty()) {
      ++line;
    }
    if (line == lines.size()) {
      break;
    }
    const size_t start = line;
    while (line < lines.size() && !Trim(lines[line]).empty()) {
      ++line;
    }
    const size_t end = line;
    if (start == 0 && Trim(lines[start]).starts_with("WEBVTT")) {
      continue;
    }
    SrtCue cue;
    if (ParseCueBlock(lines, start, end, next_id, &cue)) {
      cues.push_back(std::move(cue));
      ++next_id;
    }
  }
  std::ranges::stable_sort(cues, [](const SrtCue& left, const SrtCue& right) {
    return left.start_time < right.start_time;
  });
  return cues;
}

}

SrtParser::SrtParser(SrtParseOptions options) : options_(std::move(options)) {}

bool SrtParser::Feed(std::string_view bytes) {
  if (failed_ || bytes.size() > kMaximumInputBytes - bytes_.size()) {
    failed_ = true;
    bytes_.clear();
    return false;
  }
  bytes_.append(bytes);
  return true;
}

SrtParseResult SrtParser::Finish() {
  SrtParseResult result;
  if (failed_) {
    return result;
  }
  std::string utf8;
  if (!Decode(bytes_, options_, &utf8, &result.detected_encoding,
              &result.encoding_confidence)) {
    return result;
  }
  result.decoded = true;
  result.cues = ParseCues(utf8);
  return result;
}

SrtParseResult ParseSubRip(std::string_view bytes, SrtParseOptions options) {
  SrtParser parser(std::move(options));
  if (!parser.Feed(bytes)) {
    return {};
  }
  return parser.Finish();
}

}
