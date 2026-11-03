#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "umbra/player/text_track.h"
#include "umbra/player/webvtt_parser.h"

namespace umbra::player {
namespace {

constexpr size_t kMaximumCueTextBytes = size_t{1024} * size_t{1024};
constexpr size_t kMaximumLineBytes = size_t{1024} * size_t{1024};
constexpr size_t kMaximumCueCount = 100000;
constexpr size_t kMaximumNestingDepth = 256;
constexpr size_t kMaximumWvttSampleBytes =
    size_t{16} * size_t{1024} * size_t{1024};

std::vector<std::string_view> Split(std::string_view input, char delimiter) {
  std::vector<std::string_view> result;
  size_t begin = 0;
  while (begin <= input.size()) {
    const size_t end = input.find(delimiter, begin);
    result.push_back(input.substr(begin, end == std::string_view::npos
                                             ? input.size() - begin
                                             : end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return result;
}

std::vector<std::string_view> SplitWhitespace(std::string_view input) {
  std::vector<std::string_view> result;
  size_t position = 0;
  while (position < input.size()) {
    const size_t begin = input.find_first_not_of(" \t", position);
    if (begin == std::string_view::npos) {
      break;
    }
    const size_t end = input.find_first_of(" \t", begin);
    result.push_back(input.substr(begin, end == std::string_view::npos
                                             ? input.size() - begin
                                             : end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    position = end + 1;
  }
  return result;
}

bool ParseExponent(std::string_view value, size_t* position, int* exponent) {
  if (*position >= value.size() ||
      (value[*position] != 'e' && value[*position] != 'E')) {
    return true;
  }
  ++*position;
  bool negative = false;
  if (*position < value.size() &&
      (value[*position] == '+' || value[*position] == '-')) {
    negative = value[*position] == '-';
    ++*position;
  }
  const size_t digit_begin = *position;
  while (*position < value.size() && value[*position] >= '0' &&
         value[*position] <= '9') {
    if (*exponent < 10000) {
      *exponent = (*exponent * 10) + static_cast<int>(value[*position] - '0');
    }
    ++*position;
  }
  if (*position == digit_begin) {
    return false;
  }
  if (negative) {
    *exponent = -*exponent;
  }
  return true;
}

bool Number(std::string_view value, double* output) {
  if (value.empty()) {
    return false;
  }
  size_t index = 0;
  bool negative = false;
  if (value[index] == '+' || value[index] == '-') {
    negative = value[index] == '-';
    ++index;
  }
  long double parsed = 0.0L;
  size_t digits = 0;
  while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
    parsed = (parsed * 10.0L) + static_cast<long double>(value[index] - '0');
    ++index;
    ++digits;
  }
  if (index < value.size() && value[index] == '.') {
    ++index;
    long double scale = 0.1L;
    while (index < value.size() && value[index] >= '0' && value[index] <= '9') {
      parsed += static_cast<long double>(value[index] - '0') * scale;
      scale *= 0.1L;
      ++index;
      ++digits;
    }
  }
  int exponent = 0;
  if (!ParseExponent(value, &index, &exponent)) {
    return false;
  }
  if (digits == 0 || index != value.size()) {
    return false;
  }
  parsed *= std::pow(10.0L, exponent);
  const auto result = static_cast<double>(negative ? -parsed : parsed);
  if (!std::isfinite(result)) {
    return false;
  }
  *output = result;
  return true;
}

bool IsDigits(std::string_view value) {
  return !value.empty() && std::ranges::all_of(value, [](char character) {
    return character >= '0' && character <= '9';
  });
}

bool Unsigned(std::string_view value, uint32_t base, uint32_t* output) {
  if (value.empty() || base < 2 || base > 16) {
    return false;
  }
  uint32_t parsed = 0;
  for (char const character : value) {
    uint32_t digit = 0;
    if (character >= '0' && character <= '9') {
      digit = static_cast<uint32_t>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = static_cast<uint32_t>(character - 'a' + 10);
    } else if (character >= 'A' && character <= 'F') {
      digit = static_cast<uint32_t>(character - 'A' + 10);
    } else {
      return false;
    }
    if (digit >= base || parsed > (UINT32_MAX - digit) / base) {
      return false;
    }
    parsed = (parsed * base) + digit;
  }
  *output = parsed;
  return true;
}

bool Timestamp(std::string_view value, double* output) {
  const auto parts = Split(value, ':');
  if (parts.size() < 2 || parts.size() > 3) {
    return false;
  }
  const std::string_view seconds = parts.back();
  const size_t dot = seconds.find('.');
  if (dot == std::string_view::npos || seconds.size() - dot != 4 || dot != 2) {
    return false;
  }
  if (!IsDigits(parts[0]) || !IsDigits(seconds.substr(0, dot)) ||
      !IsDigits(seconds.substr(dot + 1))) {
    return false;
  }
  double final_seconds = 0.0;
  if (!Number(seconds, &final_seconds)) {
    return false;
  }
  if (parts.size() == 2) {
    double minutes = 0.0;
    if (!Number(parts[0], &minutes) || minutes > 59.0 ||
        final_seconds >= 60.0) {
      return false;
    }
    *output = (minutes * 60.0) + final_seconds;
  } else {
    double hours = 0.0;
    double minutes = 0.0;
    if (!Number(parts[0], &hours) || !Number(parts[1], &minutes) ||
        minutes > 59.0 || final_seconds >= 60.0) {
      return false;
    }
    *output = (hours * 3600.0) + (minutes * 60.0) + final_seconds;
  }
  return true;
}

void AppendCodepoint(std::string& output, uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool DecodeEntity(std::string_view entity, std::string& output) {
  if (entity.size() > 1 && entity[0] == '#') {
    std::string_view digits = entity.substr(1);
    uint32_t base = 10;
    if (!digits.empty() && (digits[0] == 'x' || digits[0] == 'X')) {
      digits.remove_prefix(1);
      base = 16;
    }
    uint32_t codepoint = 0;
    if (Unsigned(digits, base, &codepoint) && codepoint <= 0x10FFFF &&
        (codepoint < 0xD800 || codepoint > 0xDFFF)) {
      AppendCodepoint(output, codepoint);
      return true;
    }
    return false;
  }
  if (entity == "nbsp") {
    output += "\xC2\xA0";
  } else if (entity == "lrm") {
    output += "\xE2\x80\x8E";
  } else if (entity == "rlm") {
    output += "\xE2\x80\x8F";
  } else if (entity == "amp") {
    output.push_back('&');
  } else if (entity == "lt") {
    output.push_back('<');
  } else if (entity == "gt") {
    output.push_back('>');
  } else {
    return false;
  }
  return true;
}

std::string Decode(std::string_view text) {
  std::string output;
  size_t position = 0;
  while (position < text.size()) {
    if (text[position] != '&') {
      output.push_back(text[position++]);
      continue;
    }
    const size_t end = text.find(';', position + 1);
    if (end == std::string_view::npos || end - position > 12 ||
        !DecodeEntity(text.substr(position + 1, end - position - 1), output)) {
      output.push_back(text[position++]);
      continue;
    }
    position = end + 1;
  }
  return output;
}

struct CueTextState {
  std::string tag;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  std::string voice;
  std::string language;
  std::vector<std::string> classes;
  double timestamp = 0.0;
  bool has_timestamp = false;
  bool annotation = false;
};

void FlushCueText(std::string& segment, const CueTextState& state,
                  std::vector<StyledTextRun>& runs, std::string& annotation) {
  if (segment.empty()) {
    return;
  }
  if (state.annotation) {
    annotation += Decode(segment);
  } else {
    StyledTextRun run;
    run.text = Decode(segment);
    run.bold = state.bold;
    run.italic = state.italic;
    run.underline = state.underline;
    run.voice = state.voice;
    run.language = state.language;
    run.classes = state.classes;
    run.timestamp = state.timestamp;
    run.has_timestamp = state.has_timestamp;
    runs.push_back(std::move(run));
  }
  segment.clear();
}

bool SupportedCueTag(std::string_view tag) {
  return tag == "c" || tag == "i" || tag == "b" || tag == "u" ||
         tag == "ruby" || tag == "rt" || tag == "v" || tag == "lang";
}

bool OpenCueTag(std::string_view body, std::vector<CueTextState>& stack,
                size_t max_depth) {
  const size_t separator = body.find_first_of(" .");
  const std::string_view tag = body.substr(0, separator);
  if (!SupportedCueTag(tag)) {
    return true;
  }
  if (stack.size() > max_depth) {
    return false;
  }
  CueTextState next = stack.back();
  next.tag = std::string(tag);
  if (tag == "b") {
    next.bold = true;
  }
  if (tag == "i") {
    next.italic = true;
  }
  if (tag == "u") {
    next.underline = true;
  }
  if (tag == "rt") {
    next.annotation = true;
  }
  if (tag == "c" && separator != std::string_view::npos &&
      body[separator] == '.') {
    for (std::string_view class_name : Split(body.substr(separator + 1), '.')) {
      if (!class_name.empty()) {
        next.classes.emplace_back(class_name);
      }
    }
  }
  if ((tag == "v" || tag == "lang") && separator != std::string_view::npos) {
    const std::string value(body.substr(separator + 1));
    if (tag == "v") {
      next.voice = value;
    } else {
      next.language = value;
    }
  }
  stack.push_back(std::move(next));
  return true;
}

void CloseCueTag(std::string_view body, std::vector<CueTextState>& stack) {
  const std::string tag(body.substr(1));
  const auto found =
      std::ranges::find(stack | std::views::reverse, tag, &CueTextState::tag);
  if (found != (stack | std::views::reverse).end()) {
    const auto offset = std::distance(stack.begin(), found.base()) - 1;
    stack.erase(stack.begin() + offset, stack.end());
  }
}

bool ConsumeCueToken(std::string_view text, size_t* position,
                     std::vector<CueTextState>& stack, size_t max_depth,
                     std::vector<StyledTextRun>& runs, std::string& segment,
                     std::string& annotation, bool* valid) {
  if (text.substr(*position, 4) == "<!--") {
    FlushCueText(segment, stack.back(), runs, annotation);
    const size_t end = text.find("-->", *position + 4);
    if (end == std::string_view::npos) {
      *position = text.size();
    } else {
      *position = end + 3;
    }
    return true;
  }
  const size_t end = text.find('>', *position + 1);
  if (end == std::string_view::npos) {
    return false;
  }
  const std::string_view body = text.substr(*position + 1, end - *position - 1);
  FlushCueText(segment, stack.back(), runs, annotation);
  if (body.starts_with("--")) {
    *position = end + 1;
    return true;
  }
  double timestamp = 0.0;
  if (Timestamp(body, &timestamp)) {
    stack.back().timestamp = timestamp;
    stack.back().has_timestamp = true;
    *position = end + 1;
    return true;
  }
  if (body.size() > 1 && body.front() == '/') {
    CloseCueTag(body, stack);
  } else if (!OpenCueTag(body, stack, max_depth)) {
    *valid = false;
    return true;
  }
  *position = end + 1;
  return true;
}

std::vector<StyledTextRun> ParseCueText(std::string_view text, TextCue* cue,
                                        size_t max_depth, bool* valid) {
  std::vector<CueTextState> stack(1);
  std::vector<StyledTextRun> runs;
  std::string segment;
  std::string annotation;
  size_t position = 0;
  while (position < text.size()) {
    if (text[position] == '<' &&
        ConsumeCueToken(text, &position, stack, max_depth, runs, segment,
                        annotation, valid)) {
      if (!*valid) {
        return {};
      }
      continue;
    }
    segment.push_back(text[position++]);
  }
  FlushCueText(segment, stack.back(), runs, annotation);
  cue->ruby_annotation = std::move(annotation);
  return runs;
}

}

class WebVttParser::Impl {
 public:
  explicit Impl(WebVttOptions parser_options) : options(parser_options) {
    options.max_cue_text_bytes =
        std::min(options.max_cue_text_bytes, kMaximumCueTextBytes);
    options.max_line_bytes =
        std::min(options.max_line_bytes, kMaximumLineBytes);
    options.max_cues = std::min(options.max_cues, kMaximumCueCount);
    options.max_nesting_depth =
        std::min(options.max_nesting_depth, kMaximumNestingDepth);
  }

  void AddLine(std::string line, const std::function<void(TextCue)>& callback) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.size() > options.max_line_bytes) {
      result.warnings.push_back({line_number, "line exceeds configured limit"});
      line.clear();
    }
    ++line_number;
    if (!signature_read) {
      signature_read = true;
      if (line != "WEBVTT" && !line.starts_with("WEBVTT ")) {
        result.warnings.push_back({1, "missing WEBVTT signature"});
        invalid_signature = true;
      }
      return;
    }
    if (invalid_signature) {
      return;
    }
    if (!header_done) {
      if (line.empty()) {
        header_done = true;
      } else if (header_bytes + line.size() <= options.max_cue_text_bytes) {
        header_bytes += line.size();
        result.header_metadata.push_back(std::move(line));
      } else {
        result.warnings.push_back(
            {line_number, "header metadata limit exceeded"});
      }
      return;
    }
    if (line.empty()) {
      ProcessBlock(callback);
      block.clear();
      block_size = 0;
      return;
    }
    const size_t max_block_bytes =
        options.max_cue_text_bytes + options.max_line_bytes;
    if (line.size() > max_block_bytes - block_size) {
      result.warnings.push_back(
          {line_number, "block exceeds configured limit"});
      block.clear();
      block_size = 0;
      ignoring_block = true;
      return;
    }
    if (ignoring_block) {
      return;
    }
    block_size += line.size();
    block.push_back(std::move(line));
  }

  void ProcessBlock(const std::function<void(TextCue)>& callback) {
    ignoring_block = false;
    if (block.empty()) {
      return;
    }
    const std::string& first = block[0];
    if (first == "STYLE") {
      ParseStyleBlock();
      return;
    }
    if (first == "REGION") {
      ParseRegionBlock();
      return;
    }
    if (first == "NOTE" || first.starts_with("NOTE ") ||
        first.starts_with("NOTE\t")) {
      return;
    }
    ParseCue(callback);
  }

  [[nodiscard]] std::string JoinBlock(size_t first, char separator) const {
    std::string text;
    for (size_t i = first; i < block.size(); ++i) {
      if (!text.empty()) {
        text.push_back(separator);
      }
      text += block[i];
    }
    return text;
  }

  void ParseStyleBlock() {
    if (!options.enable_style_blocks || block.size() <= 1) {
      return;
    }
    const std::string text = JoinBlock(1, '\n');
    const size_t open = text.find('{');
    const size_t close = text.rfind('}');
    if (open == std::string::npos || close <= open) {
      return;
    }
    const std::string selector = text.substr(0, open);
    if (selector.find("::cue") == std::string::npos) {
      return;
    }
    result.styles.push_back(
        {selector, text.substr(open + 1, close - open - 1)});
  }

  void ParseRegionBlock() { ParseRegion(JoinBlock(1, ' ')); }

  void ParseRegion(const std::string& text) {
    WebVttRegion region;
    for (std::string_view const item : Split(text, ' ')) {
      ParseRegionField(item, &region);
    }
    if (!region.id.empty()) {
      result.regions.push_back(std::move(region));
    }
  }

  static void ParseRegionField(std::string_view item, WebVttRegion* region) {
    const size_t colon = item.find(':');
    if (colon == std::string_view::npos) {
      return;
    }
    const std::string_view key = item.substr(0, colon);
    const std::string_view value = item.substr(colon + 1);
    double number = 0.0;
    if (key == "id") {
      region->id = value;
    }
    if (key == "width" && !value.empty() && value.back() == '%' &&
        Number(value.substr(0, value.size() - 1), &number)) {
      region->width = number;
    }
    if (key == "lines" && !Unsigned(value, 10, &region->lines)) {
      region->lines = 3;
    }
    if (key == "scroll") {
      region->scroll_up = value == "up";
    }
    if (key == "regionanchor" || key == "viewportanchor") {
      ParseRegionAnchor(key, value, region);
    }
  }

  static void ParseRegionAnchor(std::string_view key, std::string_view value,
                                WebVttRegion* region) {
    const size_t comma = value.find(',');
    if (comma == std::string_view::npos) {
      return;
    }
    double x = 0.0;
    double y = 0.0;
    if (!Number(value.substr(0, comma), &x) ||
        !Number(value.substr(comma + 1), &y)) {
      return;
    }
    if (key == "regionanchor") {
      region->region_anchor_x = x;
      region->region_anchor_y = y;
    } else {
      region->viewport_anchor_x = x;
      region->viewport_anchor_y = y;
    }
  }

  static void ParseCueSettings(std::string_view settings, TextCue* cue) {
    for (std::string_view const setting : SplitWhitespace(settings)) {
      const size_t colon = setting.find(':');
      if (colon == std::string_view::npos) {
        continue;
      }
      ApplyCueSetting(setting.substr(0, colon), setting.substr(colon + 1), cue);
    }
  }

  static void ApplyCueSetting(std::string_view key, std::string_view value,
                              TextCue* cue) {
    double number = 0.0;
    if (key == "vertical") {
      cue->vertical = value == "rl" || value == "lr";
    }
    const size_t comma = value.find(',');
    const std::string_view number_text = value.substr(0, comma);
    if ((key == "position" || key == "size") && !number_text.empty() &&
        number_text.back() == '%' &&
        Number(number_text.substr(0, number_text.size() - 1), &number)) {
      if (key == "position") {
        cue->position = std::clamp(number, 0.0, 100.0);
      } else {
        cue->size = std::clamp(number, 0.0, 100.0);
      }
    }
    if (key == "line") {
      ParseLineSetting(value, cue);
    }
    if (key == "align" && IsCueAlignment(value)) {
      cue->alignment = std::string(value);
    }
    if (key == "position" && comma != std::string_view::npos) {
      const std::string_view alignment = value.substr(comma + 1);
      if (IsPositionAlignment(alignment)) {
        cue->position_alignment = std::string(alignment);
      }
    }
    if (key == "region") {
      cue->region = std::string(value);
    }
  }

  [[nodiscard]] static bool IsCueAlignment(std::string_view value) {
    return value == "start" || value == "center" || value == "end" ||
           value == "left" || value == "right";
  }

  [[nodiscard]] static bool IsPositionAlignment(std::string_view value) {
    return value == "line-left" || value == "center" || value == "line-right" ||
           value == "auto";
  }

  static void ParseLineSetting(std::string_view value, TextCue* cue) {
    const size_t comma = value.find(',');
    const std::string_view line = value.substr(0, comma);
    double number = 0.0;
    if (!line.empty() && line.back() == '%' &&
        Number(line.substr(0, line.size() - 1), &number)) {
      cue->line = number;
      cue->snap_to_lines = false;
    } else if (Number(line, &number)) {
      cue->line = number;
      cue->snap_to_lines = true;
    }
    if (comma != std::string_view::npos) {
      const std::string_view alignment = value.substr(comma + 1);
      if (alignment == "start" || alignment == "center" || alignment == "end") {
        cue->line_alignment = std::string(alignment);
      }
    }
  }

  void ParseCue(const std::function<void(TextCue)>& callback) {
    std::string id;
    const size_t timing_index =
        block[0].find("-->") == std::string::npos ? 1 : 0;
    if (timing_index != 0) {
      id = block[0];
    }
    if (timing_index >= block.size()) {
      result.warnings.push_back(
          {line_number - block.size(), "cue has no timestamp"});
      return;
    }
    TextCue cue;
    std::string_view settings;
    if (!ParseCueTiming(block[timing_index], &cue, &settings)) {
      result.warnings.push_back(
          {line_number - block.size(), "invalid cue timestamp"});
      return;
    }
    cue.id = std::hash<std::string>{}(id + std::to_string(cue.start_time) +
                                      std::to_string(cue_count));
    ParseCueSettings(settings, &cue);
    const std::string text = JoinBlock(timing_index + 1, '\n');
    if (text.size() > options.max_cue_text_bytes ||
        cue_count >= options.max_cues) {
      result.warnings.push_back(
          {line_number - block.size(), "cue limit exceeded"});
      return;
    }
    bool valid = true;
    cue.runs = ParseCueText(text, &cue, options.max_nesting_depth, &valid);
    if (!valid) {
      result.warnings.push_back(
          {line_number - block.size(), "cue text nesting limit exceeded"});
      return;
    }
    ++cue_count;
    if (callback) {
      callback(std::move(cue));
    } else {
      result.warnings.push_back(
          {line_number - block.size(), "cue has no output callback"});
    }
  }

  static bool ParseCueTiming(std::string_view timing, TextCue* cue,
                             std::string_view* settings) {
    const size_t arrow = timing.find("-->");
    if (arrow == std::string_view::npos) {
      return false;
    }
    std::string_view start = timing.substr(0, arrow);
    while (!start.empty() && (start.back() == ' ' || start.back() == '\t')) {
      start.remove_suffix(1);
    }
    const size_t end_begin = timing.find_first_not_of(" \t", arrow + 3);
    if (end_begin == std::string_view::npos) {
      return false;
    }
    const std::string_view right = timing.substr(end_begin);
    const size_t separator = right.find_first_of(" \t");
    const std::string_view end = right.substr(0, separator);
    if (!Timestamp(start, &cue->start_time) ||
        !Timestamp(end, &cue->end_time) || cue->end_time < cue->start_time) {
      return false;
    }
    if (separator == std::string_view::npos) {
      *settings = std::string_view();
    } else {
      const size_t settings_begin = right.find_first_not_of(" \t", separator);
      *settings = settings_begin == std::string_view::npos
                      ? std::string_view()
                      : right.substr(settings_begin);
    }
    return true;
  }

  WebVttOptions options;
  WebVttResult result;
  std::vector<std::string> block;
  std::string partial;
  size_t block_size = 0;
  size_t header_bytes = 0;
  size_t line_number = 1;
  bool signature_read = false;
  bool header_done = false;
  bool invalid_signature = false;
  bool ignoring_block = false;
  size_t cue_count = 0;
  bool finished = false;
  std::function<void(TextCue)> callback;
};

WebVttParser::WebVttParser(WebVttOptions options)
    : impl_(std::make_unique<Impl>(options)) {}
WebVttParser::~WebVttParser() = default;

void WebVttParser::Feed(std::string_view bytes,
                        const std::function<void(TextCue)>& on_cue) {
  if (impl_->finished) {
    return;
  }
  if (on_cue) {
    impl_->callback = on_cue;
  }
  for (char const byte : bytes) {
    if (byte == '\n') {
      impl_->AddLine(std::move(impl_->partial), impl_->callback);
      impl_->partial.clear();
    } else if (impl_->partial.size() <= impl_->options.max_line_bytes) {
      impl_->partial.push_back(byte);
    }
  }
}

WebVttResult WebVttParser::Finish() {
  if (!impl_->finished) {
    if (!impl_->partial.empty()) {
      impl_->AddLine(std::move(impl_->partial), impl_->callback);
    }
    impl_->ProcessBlock(impl_->callback);
    if (!impl_->signature_read) {
      impl_->result.warnings.push_back({1, "missing WEBVTT signature"});
    }
    impl_->finished = true;
  }
  return std::move(impl_->result);
}

WebVttResult WebVttParser::Parse(std::string_view bytes,
                                 WebVttOptions options) {
  WebVttParser parser(options);
  WebVttResult result;
  parser.Feed(
      bytes, [&result](TextCue cue) { result.cues.push_back(std::move(cue)); });
  WebVttResult metadata = parser.Finish();
  result.regions = std::move(metadata.regions);
  result.styles = std::move(metadata.styles);
  result.header_metadata = std::move(metadata.header_metadata);
  result.warnings = std::move(metadata.warnings);
  return result;
}

namespace {

uint32_t BoxSize(std::string_view bytes, size_t offset) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset])) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1]))
          << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3]));
}

struct WvttCueBox {
  std::string payload;
  std::string settings;
  std::string identifier;
  bool has_payload = false;
};

bool ParseCueBox(std::string_view body, WvttCueBox* cue) {
  size_t offset = 0;
  while (offset < body.size()) {
    if (body.size() - offset < 8) {
      return false;
    }
    const uint32_t size = BoxSize(body, offset);
    if (size < 8 || size > body.size() - offset) {
      return false;
    }
    const std::string_view type = body.substr(offset + 4, 4);
    const std::string_view content = body.substr(offset + 8, size - 8);
    if (type == "payl") {
      cue->payload.assign(content);
      cue->has_payload = true;
    } else if (type == "sttg") {
      cue->settings.assign(content);
    } else if (type == "iden") {
      cue->identifier.assign(content);
    }
    offset += size;
  }
  return true;
}

std::string TwoDigits(uint64_t value) {
  std::string text = std::to_string(value);
  if (text.size() < 2) {
    text.insert(text.begin(), '0');
  }
  return text;
}

std::string ThreeDigits(uint64_t value) {
  std::string text = std::to_string(value);
  while (text.size() < 3) {
    text.insert(text.begin(), '0');
  }
  return text;
}

std::string FormatTimestamp(double seconds) {
  const auto millis = static_cast<uint64_t>(seconds * 1000.0);
  const uint64_t hours = millis / 3600000;
  const uint64_t minutes = (millis / 60000) % 60;
  const uint64_t whole_seconds = (millis / 1000) % 60;
  const uint64_t remainder = millis % 1000;
  return TwoDigits(hours) + ":" + TwoDigits(minutes) + ":" +
         TwoDigits(whole_seconds) + "." + ThreeDigits(remainder);
}

void MergeParsed(WebVttResult parsed, WebVttResult* output) {
  output->cues.insert(output->cues.end(),
                      std::make_move_iterator(parsed.cues.begin()),
                      std::make_move_iterator(parsed.cues.end()));
  output->warnings.insert(output->warnings.end(),
                          std::make_move_iterator(parsed.warnings.begin()),
                          std::make_move_iterator(parsed.warnings.end()));
}

WebVttResult ParseWvttCue(const WvttCueBox& cue, double start_time,
                          double end_time, WebVttOptions options) {
  std::string document = "WEBVTT\n\n";
  if (!cue.identifier.empty()) {
    document += cue.identifier + "\n";
  }
  document += FormatTimestamp(start_time) + " --> " + FormatTimestamp(end_time);
  if (!cue.settings.empty()) {
    document += " " + cue.settings;
  }
  document += "\n" + cue.payload + "\n";
  return WebVttParser::Parse(document, options);
}

}

WebVttResult WebVttParser::ParseWvttSample(std::string_view bytes,
                                           double start_time, double end_time,
                                           WebVttOptions options) {
  WebVttResult result;
  if (bytes.size() > kMaximumWvttSampleBytes) {
    result.warnings.push_back({0, "wvtt sample exceeds configured limit"});
    return result;
  }
  if (!std::isfinite(start_time) || !std::isfinite(end_time) ||
      start_time < 0 || end_time < start_time ||
      end_time >
          static_cast<double>(std::numeric_limits<uint64_t>::max()) / 1000.0) {
    result.warnings.push_back({0, "invalid sample interval"});
    return result;
  }
  bool empty_sample = false;
  size_t offset = 0;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < 8) {
      result.warnings.push_back({0, "truncated wvtt box header"});
      return result;
    }
    const uint32_t size = BoxSize(bytes, offset);
    if (size < 8 || size > bytes.size() - offset) {
      result.warnings.push_back({0, "invalid wvtt box length"});
      return result;
    }
    const std::string_view type = bytes.substr(offset + 4, 4);
    const std::string_view body = bytes.substr(offset + 8, size - 8);
    if (type == "vtte") {
      empty_sample = true;
    } else if (type == "vtta") {
      result.header_metadata.emplace_back(body);
    } else if (type == "vttc") {
      WvttCueBox cue;
      if (!ParseCueBox(body, &cue)) {
        result.warnings.push_back({0, "invalid child box in vttc"});
      } else if (cue.has_payload) {
        MergeParsed(ParseWvttCue(cue, start_time, end_time, options), &result);
      }
    }
    offset += size;
  }
  if (empty_sample) {
    result.cues.clear();
  }
  return result;
}

}
