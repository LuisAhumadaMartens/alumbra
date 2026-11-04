#include "umbra/player/ass_parser.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "umbra/player/text_track.h"

namespace umbra::player {
namespace {

constexpr std::size_t kHardScriptLimit = 16ULL * 1024ULL * 1024ULL;
constexpr std::size_t kHardEventLimit = 100000;
constexpr std::size_t kHardEventTextLimit = 1024ULL * 1024ULL;

struct Style {
  std::string font;
  double size = 36.0;
  uint32_t color = 0xFFFFFFFF;
  bool has_color = true;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  int alignment = 2;
};

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

std::string Lower(std::string_view value) {
  std::string result(value);
  for (char& character : result) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return result;
}

std::vector<std::string_view> Split(std::string_view value, char delimiter) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t end = value.find(delimiter, begin);
    fields.push_back(value.substr(begin, end == std::string_view::npos
                                             ? value.size() - begin
                                             : end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return fields;
}

bool Number(std::string_view value, double* output) {
  value = Trim(value);
  if (value.empty()) {
    return false;
  }
  double parsed = 0.0;
  const char* const begin = value.data();
  const char* const end =
      std::next(begin, static_cast<std::ptrdiff_t>(value.size()));
  const auto conversion = std::from_chars(begin, end, parsed);
  if (conversion.ec != std::errc() || conversion.ptr != end ||
      !std::isfinite(parsed)) {
    return false;
  }
  *output = parsed;
  return true;
}

bool Timestamp(std::string_view value, double* output) {
  const std::vector<std::string_view> parts = Split(value, ':');
  if (parts.size() != 3) {
    return false;
  }
  double hours = 0.0;
  double minutes = 0.0;
  double seconds = 0.0;
  if (!Number(parts[0], &hours) || !Number(parts[1], &minutes) ||
      !Number(parts[2], &seconds) || hours < 0.0 || minutes < 0.0 ||
      minutes >= 60.0 || seconds < 0.0 || seconds >= 60.0) {
    return false;
  }
  *output = (hours * 3600.0) + (minutes * 60.0) + seconds;
  return std::isfinite(*output);
}

bool Integer(std::string_view value, int* output) {
  value = Trim(value);
  if (value.empty()) {
    return false;
  }
  int parsed = 0;
  const char* const begin = value.data();
  const char* const end =
      std::next(begin, static_cast<std::ptrdiff_t>(value.size()));
  const auto conversion = std::from_chars(begin, end, parsed);
  if (conversion.ec != std::errc() || conversion.ptr != end) {
    return false;
  }
  *output = parsed;
  return true;
}

bool AssColor(std::string_view value, uint32_t* output) {
  value = Trim(value);
  if (value.starts_with("&H") || value.starts_with("&h")) {
    value.remove_prefix(2);
  }
  if (!value.empty() && (value.back() == '&')) {
    value.remove_suffix(1);
  }
  if (value.size() != 6 && value.size() != 8) {
    return false;
  }
  uint32_t parsed = 0;
  const char* const begin = value.data();
  const char* const end =
      std::next(begin, static_cast<std::ptrdiff_t>(value.size()));
  const auto conversion = std::from_chars(begin, end, parsed, 16);
  if (conversion.ec != std::errc() || conversion.ptr != end) {
    return false;
  }
  if (value.size() <= 6) {
    parsed |= 0xFF000000;
  }
  const uint32_t alpha = (parsed >> 24) & 0xFF;
  const uint32_t blue = (parsed >> 16) & 0xFF;
  const uint32_t green = (parsed >> 8) & 0xFF;
  const uint32_t red = parsed & 0xFF;
  *output = ((0xFF - alpha) << 24) | (red << 16) | (green << 8) | blue;
  return true;
}

std::vector<std::string> Names(std::string_view format) {
  std::vector<std::string> result;
  for (const std::string_view field : Split(format, ',')) {
    result.push_back(Lower(Trim(field)));
  }
  return result;
}

std::map<std::string, std::string> Fields(
    const std::vector<std::string>& names,
    const std::vector<std::string_view>& values) {
  std::map<std::string, std::string> result;
  const std::size_t count = std::min(names.size(), values.size());
  for (std::size_t index = 0; index < count; ++index) {
    result[names[index]] = std::string(Trim(values[index]));
  }
  return result;
}

std::vector<std::string_view> SplitEventFields(std::string_view value,
                                               std::size_t field_count) {
  std::vector<std::string_view> fields;
  if (field_count == 0) {
    return fields;
  }
  std::size_t begin = 0;
  for (std::size_t index = 0; index + 1 < field_count; ++index) {
    const std::size_t end = value.find(',', begin);
    if (end == std::string_view::npos) {
      return {};
    }
    fields.push_back(value.substr(begin, end - begin));
    begin = end + 1;
  }
  fields.push_back(value.substr(begin));
  return fields;
}

void ApplyStyle(const std::map<std::string, std::string>& fields,
                Style* style) {
  for (const auto& [name, value] : fields) {
    if (name == "fontname") {
      style->font = value;
    } else if (name == "fontsize") {
      Number(value, &style->size);
    } else if (name == "primarycolour" || name == "primarycolor") {
      style->has_color = AssColor(value, &style->color);
    } else if (name == "bold") {
      int parsed = 0;
      style->bold = Integer(value, &parsed) && parsed != 0;
    } else if (name == "italic") {
      int parsed = 0;
      style->italic = Integer(value, &parsed) && parsed != 0;
    } else if (name == "underline") {
      int parsed = 0;
      style->underline = Integer(value, &parsed) && parsed != 0;
    } else if (name == "alignment") {
      Integer(value, &style->alignment);
    }
  }
  style->alignment = std::clamp(style->alignment, 1, 9);
  if (!std::isfinite(style->size) || style->size <= 0.0) {
    style->size = 36.0;
  }
}

void AddWarning(AssResult* result, std::size_t line, std::string reason) {
  result->warnings.push_back({line, std::move(reason)});
}

void SetAlignment(int alignment, TextCue* cue) {
  const int horizontal = (alignment - 1) % 3;
  if (horizontal == 0) {
    cue->alignment = "left";
    cue->position_alignment = "line-left";
  } else if (horizontal == 1) {
    cue->alignment = "center";
    cue->position_alignment = "center";
  } else {
    cue->alignment = "right";
    cue->position_alignment = "line-right";
  }
  const int vertical = (alignment - 1) / 3;
  if (vertical == 0) {
    cue->line = 100.0;
    cue->line_alignment = "end";
  } else if (vertical == 1) {
    cue->line = 50.0;
    cue->line_alignment = "center";
  } else {
    cue->line = 0.0;
    cue->line_alignment = "start";
  }
  cue->snap_to_lines = false;
}

bool ApplyToggle(std::string_view tag, Style* style, bool* unsupported) {
  if (tag.empty() ||
      (tag.front() != 'b' && tag.front() != 'i' && tag.front() != 'u')) {
    return false;
  }
  int enabled = 1;
  if (tag.size() > 1 && !Integer(tag.substr(1), &enabled)) {
    *unsupported = true;
    return true;
  }
  if (tag.front() == 'b') {
    style->bold = enabled != 0;
  } else if (tag.front() == 'i') {
    style->italic = enabled != 0;
  } else {
    style->underline = enabled != 0;
  }
  return true;
}

bool ApplyColor(std::string_view tag, Style* style, bool* unsupported) {
  if (!tag.starts_with("1c") && !tag.starts_with("c")) {
    return false;
  }
  const std::size_t offset = tag.starts_with("1c") ? 2 : 1;
  uint32_t color = 0;
  if (AssColor(tag.substr(offset), &color)) {
    style->color = color;
    style->has_color = true;
  } else {
    *unsupported = true;
  }
  return true;
}

bool ApplyAlignment(std::string_view tag, Style* style, TextCue* cue,
                    bool* unsupported) {
  if (!tag.starts_with("an") || tag.size() <= 2) {
    return false;
  }
  int alignment = 0;
  if (Integer(tag.substr(2), &alignment) && alignment >= 1 && alignment <= 9) {
    style->alignment = alignment;
    SetAlignment(alignment, cue);
  } else {
    *unsupported = true;
  }
  return true;
}

bool ApplyPosition(std::string_view tag, TextCue* cue, int play_res_x,
                   int play_res_y, bool* unsupported) {
  if (!tag.starts_with("pos(") || tag.back() != ')') {
    return false;
  }
  const std::vector<std::string_view> xy =
      Split(tag.substr(4, tag.size() - 5), ',');
  double x = 0.0;
  double y = 0.0;
  if (xy.size() == 2 && Number(xy[0], &x) && Number(xy[1], &y) &&
      play_res_x > 0 && play_res_y > 0) {
    cue->position = std::clamp((x * 100.0) / play_res_x, 0.0, 100.0);
    cue->line = std::clamp((y * 100.0) / play_res_y, 0.0, 100.0);
    cue->snap_to_lines = false;
  } else {
    *unsupported = true;
  }
  return true;
}

bool ApplyFade(std::string_view tag, TextCue* cue, bool* unsupported) {
  if (!tag.starts_with("fad(") || tag.back() != ')') {
    return false;
  }
  const std::vector<std::string_view> values =
      Split(tag.substr(4, tag.size() - 5), ',');
  int fade_in_ms = 0;
  int fade_out_ms = 0;
  if (values.size() == 2 && Integer(values[0], &fade_in_ms) &&
      Integer(values[1], &fade_out_ms) && fade_in_ms >= 0 && fade_out_ms >= 0) {
    cue->fade_in_seconds = static_cast<double>(fade_in_ms) / 1000.0;
    cue->fade_out_seconds = static_cast<double>(fade_out_ms) / 1000.0;
  } else {
    *unsupported = true;
  }
  return true;
}

void ApplyOverride(std::string_view tag, const Style& base_style, Style* style,
                   TextCue* cue, int play_res_x, int play_res_y,
                   bool* unsupported) {
  if (tag.empty()) {
    return;
  }
  if (ApplyToggle(tag, style, unsupported) ||
      ApplyColor(tag, style, unsupported) ||
      ApplyAlignment(tag, style, cue, unsupported) ||
      ApplyPosition(tag, cue, play_res_x, play_res_y, unsupported) ||
      ApplyFade(tag, cue, unsupported)) {
    return;
  }
  if (tag == "r") {
    *style = base_style;
    SetAlignment(style->alignment, cue);
    return;
  }
  if (tag.starts_with("r")) {
    *unsupported = true;
    return;
  }
  *unsupported = true;
}

void ParseOverrideBlock(std::string_view block, const Style& base,
                        Style* current, TextCue* cue, int play_res_x,
                        int play_res_y, bool* unsupported) {
  if (block.find("\\p") != std::string_view::npos ||
      block.find("\\k") != std::string_view::npos ||
      block.find("\\t(") != std::string_view::npos ||
      block.find("\\move(") != std::string_view::npos) {
    *unsupported = true;
  }
  std::size_t tag_start = 0;
  while (tag_start < block.size()) {
    const std::size_t slash = block.find('\\', tag_start);
    if (slash == std::string_view::npos) {
      break;
    }
    std::size_t next = block.find('\\', slash + 1);
    if (next == std::string_view::npos) {
      next = block.size();
    }
    ApplyOverride(block.substr(slash + 1, next - slash - 1), base, current, cue,
                  play_res_x, play_res_y, unsupported);
    tag_start = next;
  }
}

bool AppendEscapedText(std::string_view text, std::size_t* index,
                       std::string* segment) {
  if (text[*index] != '\\' || *index + 1 >= text.size()) {
    return false;
  }
  const char escaped = text[*index + 1];
  if (escaped == 'N' || escaped == 'n') {
    segment->push_back('\n');
  } else if (escaped == 'h') {
    *segment += "\xC2\xA0";
  } else {
    return false;
  }
  *index += 2;
  return true;
}

std::vector<StyledTextRun> ParseText(std::string_view text, const Style& base,
                                     TextCue* cue, int play_res_x,
                                     int play_res_y, bool* unsupported) {
  Style current = base;
  std::vector<StyledTextRun> runs;
  std::string segment;
  auto flush = [&] {
    if (segment.empty()) {
      return;
    }
    StyledTextRun run;
    run.text = std::move(segment);
    run.bold = current.bold;
    run.italic = current.italic;
    run.underline = current.underline;
    run.font_family = current.font;
    run.font_size = current.size;
    run.foreground_argb = current.color;
    run.has_foreground_color = current.has_color;
    runs.push_back(std::move(run));
    segment.clear();
  };
  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '{') {
      const std::size_t end = text.find('}', index + 1);
      if (end == std::string_view::npos) {
        segment.append(text.substr(index));
        break;
      }
      flush();
      ParseOverrideBlock(text.substr(index + 1, end - index - 1), base,
                         &current, cue, play_res_x, play_res_y, unsupported);
      index = end + 1;
      continue;
    }
    if (AppendEscapedText(text, &index, &segment)) {
      continue;
    }
    segment.push_back(text[index++]);
  }
  flush();
  return runs;
}

std::string TimestampText(double seconds) {
  const auto centiseconds = static_cast<int64_t>(std::llround(seconds * 100.0));
  const int64_t hours = centiseconds / 360000;
  const int64_t minutes = (centiseconds / 6000) % 60;
  const int64_t rest = centiseconds % 6000;
  const int64_t whole = rest / 100;
  const int64_t fraction = rest % 100;
  auto two = [](int64_t value) {
    std::string result = std::to_string(value);
    if (result.size() < 2) {
      result.insert(result.begin(), '0');
    }
    return result;
  };
  return std::to_string(hours) + ":" + two(minutes) + ":" + two(whole) + "." +
         two(fraction);
}

struct ParseState {
  std::map<std::string, Style> styles;
  Style default_style;
  std::vector<std::string> style_format;
  std::vector<std::string> event_format;
  bool in_styles = false;
  bool in_events = false;
  std::size_t event_count = 0;
};

void ParseStyleLine(std::string_view line, ParseState* state) {
  const std::vector<std::string_view> values = Split(Trim(line.substr(6)), ',');
  const auto fields = Fields(state->style_format, values);
  Style style;
  ApplyStyle(fields, &style);
  const auto name = fields.find("name");
  if (name != fields.end() && !name->second.empty()) {
    state->styles[name->second] = style;
  }
}

void ParseDialogueLine(std::string_view line, std::size_t line_number,
                       const AssOptions& options, ParseState* state,
                       AssResult* result) {
  if (state->event_count >= options.max_events) {
    AddWarning(result, line_number, "ASS event count limit exceeded");
    return;
  }
  if (line.size() > options.max_event_bytes) {
    AddWarning(result, line_number, "ASS event exceeds configured byte limit");
    return;
  }
  ++state->event_count;
  const std::vector<std::string_view> values =
      SplitEventFields(Trim(line.substr(9)), state->event_format.size());
  if (values.size() < state->event_format.size() ||
      state->event_format.empty()) {
    AddWarning(result, line_number, "invalid ASS dialogue event");
    return;
  }
  const auto fields = Fields(state->event_format, values);
  double start_time = 0.0;
  double end_time = 0.0;
  const auto start = fields.find("start");
  const auto finish = fields.find("end");
  const auto text = fields.find("text");
  if (start == fields.end() || finish == fields.end() || text == fields.end() ||
      !Timestamp(start->second, &start_time) ||
      !Timestamp(finish->second, &end_time) || end_time <= start_time) {
    AddWarning(result, line_number, "invalid ASS dialogue event");
    return;
  }
  Style style = state->default_style;
  const auto style_name = fields.find("style");
  if (style_name != fields.end()) {
    const auto found = state->styles.find(style_name->second);
    if (found != state->styles.end()) {
      style = found->second;
    }
  }
  TextCue cue;
  cue.id = result->cues.size() + 1;
  cue.start_time = start_time;
  cue.end_time = end_time;
  SetAlignment(style.alignment, &cue);
  bool unsupported = false;
  cue.runs = ParseText(text->second, style, &cue, result->play_res_x,
                       result->play_res_y, &unsupported);
  const double duration = cue.end_time - cue.start_time;
  cue.fade_in_seconds = std::min(cue.fade_in_seconds, duration);
  cue.fade_out_seconds = std::min(cue.fade_out_seconds, duration);
  if (unsupported) {
    AddWarning(result, line_number,
               "out-of-subset override tags degraded to styled text");
  }
  result->cues.push_back(std::move(cue));
}

void ParseScriptLine(std::string_view line, std::size_t line_number,
                     const AssOptions& options, ParseState* state,
                     AssResult* result) {
  if (!line.empty() && line.front() == '[' && line.back() == ']') {
    const std::string section = Lower(line);
    state->in_styles = section == "[v4+ styles]" || section == "[v4 styles]";
    state->in_events = section == "[events]";
    return;
  }
  if (line.starts_with("PlayResX:")) {
    Integer(Trim(line.substr(9)), &result->play_res_x);
  } else if (line.starts_with("PlayResY:")) {
    Integer(Trim(line.substr(9)), &result->play_res_y);
  } else if (state->in_styles &&
             (line.starts_with("Format:") || line.starts_with("Format :"))) {
    state->style_format = Names(Trim(line.substr(line.find(':') + 1)));
  } else if (state->in_styles && line.starts_with("Style:")) {
    ParseStyleLine(line, state);
  } else if (state->in_events && line.starts_with("Format:")) {
    state->event_format = Names(Trim(line.substr(7)));
  } else if (state->in_events && line.starts_with("Dialogue:")) {
    ParseDialogueLine(line, line_number, options, state, result);
  }
}

std::vector<std::string> MatroskaEventFormat(std::string_view codec_private) {
  bool in_events = false;
  std::size_t begin = 0;
  while (begin <= codec_private.size()) {
    const std::size_t end = codec_private.find('\n', begin);
    const std::string_view line = Trim(codec_private.substr(
        begin, end == std::string_view::npos ? codec_private.size() - begin
                                             : end - begin));
    if (!line.empty() && line.front() == '[' && line.back() == ']') {
      in_events = Lower(line) == "[events]";
    } else if (in_events && line.starts_with("Format:")) {
      return Names(Trim(line.substr(7)));
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return {};
}

void AddEventTimes(std::vector<std::string>* fields) {
  const auto start = std::ranges::find(*fields, "start");
  const auto end = std::ranges::find(*fields, "end");
  if (start != fields->end() && end != fields->end()) {
    return;
  }
  const auto layer = std::ranges::find(*fields, "layer");
  auto insertion = layer == fields->end() ? fields->begin() : std::next(layer);
  insertion = fields->insert(insertion, "start");
  fields->insert(std::next(insertion), "end");
}

void AppendMatroskaHeader(std::string* assembled,
                          std::string_view codec_private,
                          const std::vector<std::string>& event_fields) {
  bool in_events_section = false;
  bool format_replaced = false;
  std::size_t begin = 0;
  while (begin <= codec_private.size()) {
    const std::size_t end = codec_private.find('\n', begin);
    const std::string_view line = Trim(codec_private.substr(
        begin, end == std::string_view::npos ? codec_private.size() - begin
                                             : end - begin));
    if (!line.empty() && line.front() == '[' && line.back() == ']') {
      in_events_section = Lower(line) == "[events]";
    }
    if (in_events_section && !format_replaced && line.starts_with("Format:")) {
      *assembled += "Format: ";
      for (std::size_t index = 0; index < event_fields.size(); ++index) {
        if (index != 0) {
          assembled->push_back(',');
        }
        *assembled += event_fields[index];
      }
      format_replaced = true;
    } else {
      assembled->append(codec_private.substr(
          begin, end == std::string_view::npos ? codec_private.size() - begin
                                               : end - begin));
    }
    if (end == std::string_view::npos) {
      break;
    }
    assembled->push_back('\n');
    begin = end + 1;
  }
}

void AppendMatroskaDialogue(std::string* assembled,
                            const std::vector<std::string>& event_fields,
                            const std::map<std::string, std::string>& supplied,
                            double start_time, double end_time) {
  *assembled += "Dialogue: ";
  for (std::size_t index = 0; index < event_fields.size(); ++index) {
    if (index != 0) {
      assembled->push_back(',');
    }
    const std::string& name = event_fields[index];
    if (name == "start") {
      *assembled += TimestampText(start_time);
    } else if (name == "end") {
      *assembled += TimestampText(end_time);
    } else {
      const auto field = supplied.find(name);
      if (field != supplied.end()) {
        *assembled += field->second;
      }
    }
  }
  assembled->push_back('\n');
}

std::string AssembleMatroskaEvent(
    std::string_view codec_private,
    const std::vector<std::string>& event_fields,
    const std::map<std::string, std::string>& supplied, double start_time,
    double end_time) {
  std::string assembled;
  AppendMatroskaHeader(&assembled, codec_private, event_fields);
  if (!assembled.empty() && assembled.back() != '\n') {
    assembled.push_back('\n');
  }
  AppendMatroskaDialogue(&assembled, event_fields, supplied, start_time,
                         end_time);
  return assembled;
}

}

AssResult AssParser::Parse(std::string_view script, AssOptions options) {
  AssResult result;
  options.max_script_bytes =
      std::min(options.max_script_bytes, kHardScriptLimit);
  options.max_events = std::min(options.max_events, kHardEventLimit);
  options.max_event_bytes =
      std::min(options.max_event_bytes, kHardEventTextLimit);
  if (script.size() > options.max_script_bytes) {
    AddWarning(&result, 0, "ASS script exceeds configured byte limit");
    return result;
  }
  ParseState state;
  std::size_t line_number = 0;
  std::size_t begin = 0;
  while (begin <= script.size()) {
    ++line_number;
    const std::size_t end = script.find('\n', begin);
    const std::string_view line = Trim(script.substr(
        begin,
        end == std::string_view::npos ? script.size() - begin : end - begin));
    ParseScriptLine(line, line_number, options, &state, &result);
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  if (result.play_res_x <= 0 || result.play_res_y <= 0) {
    result.play_res_x = 384;
    result.play_res_y = 288;
    AddWarning(&result, 0, "invalid PlayRes; using default layout resolution");
  }
  return result;
}

AssResult AssParser::ParseMatroskaEvent(std::string_view codec_private,
                                        std::string_view event,
                                        double start_time, double end_time,
                                        AssOptions options) {
  if (codec_private.size() > options.max_script_bytes ||
      !std::isfinite(start_time) || !std::isfinite(end_time) ||
      start_time < 0.0 || end_time <= start_time) {
    AssResult result;
    AddWarning(&result, 0, "invalid Matroska ASS event or script header");
    return result;
  }
  const double maximum_timestamp =
      (static_cast<double>(std::numeric_limits<int64_t>::max()) / 100.0) - 1.0;
  if (end_time > maximum_timestamp) {
    AssResult result;
    AddWarning(&result, 0, "Matroska ASS event time is out of range");
    return result;
  }
  std::vector<std::string> event_fields = MatroskaEventFormat(codec_private);
  if (event_fields.empty()) {
    AssResult result = Parse(codec_private, options);
    AddWarning(&result, 0, "Matroska ASS header has no event format");
    return result;
  }
  AddEventTimes(&event_fields);
  std::vector<std::string> block_fields;
  for (const std::string& name : event_fields) {
    if (name != "start" && name != "end") {
      block_fields.push_back(name);
    }
  }
  const std::vector<std::string_view> values =
      SplitEventFields(event, block_fields.size());
  if (values.size() != block_fields.size()) {
    AssResult result = Parse(codec_private, options);
    AddWarning(&result, 0, "Matroska ASS event has too few fields");
    return result;
  }
  const auto supplied = Fields(block_fields, values);
  const std::string assembled = AssembleMatroskaEvent(
      codec_private, event_fields, supplied, start_time, end_time);
  return Parse(assembled, options);
}

}
