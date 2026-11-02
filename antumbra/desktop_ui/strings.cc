#include "alumbra/desktop_ui/strings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/strings/str_cat.h"
#include "umbra/ui/types.h"

namespace alumbra::desktop_ui {
namespace {

struct Entry {
  std::string_view key;
  std::string_view text;
};

constexpr std::array<Entry, 14> kEnglish{{
    {.key = "error.title", .text = "Playback error"},
    {.key = "error.message", .text = "Playback stopped. Error {code}."},
    {.key = "time.live", .text = "LIVE"},
    {.key = "control.seek", .text = "Seek"},
    {.key = "control.volume", .text = "Volume"},
    {.key = "control.play", .text = "Play"},
    {.key = "control.pause", .text = "Pause"},
    {.key = "control.mute", .text = "Mute"},
    {.key = "control.settings", .text = "Settings"},
    {.key = "control.fullscreen", .text = "Full screen"},
    {.key = "control.skip_ad", .text = "Skip ad"},
    {.key = "announce.play", .text = "Playing"},
    {.key = "announce.pause", .text = "Paused"},
    {.key = "announce.ad_started", .text = "Advertisement"},
}};

std::string_view Template(std::string_view key) {
  for (const Entry& entry : kEnglish) {
    if (entry.key == key) {
      return entry.text;
    }
  }
  return key;
}

std::string ArgumentText(const umbra::ui::TextArg& arg) {
  struct Visitor {
    std::string operator()(std::int64_t value) const {
      return absl::StrCat(value);
    }
    std::string operator()(double value) const { return absl::StrCat(value); }
    std::string operator()(const std::string& value) const { return value; }
  };
  return std::visit(Visitor{}, arg.value);
}

std::string Substitute(std::string_view text,
                       const std::vector<umbra::ui::TextArg>& args) {
  std::string result;
  std::size_t index = 0;
  while (index < text.size()) {
    if (text[index] == '{') {
      const std::size_t close = text.find('}', index);
      if (close != std::string_view::npos) {
        const std::string_view name = text.substr(index + 1, close - index - 1);
        bool replaced = false;
        for (const umbra::ui::TextArg& arg : args) {
          if (arg.name == name) {
            absl::StrAppend(&result, ArgumentText(arg));
            replaced = true;
            break;
          }
        }
        if (!replaced) {
          absl::StrAppend(&result, text.substr(index, close - index + 1));
        }
        index = close + 1;
        continue;
      }
    }
    result.push_back(text[index]);
    ++index;
  }
  return result;
}

}

std::string PseudoAccent(std::string_view text) {
  struct Mapping {
    char plain;
    std::string_view accented;
  };
  static constexpr std::array<Mapping, 15> kMap{{
      {.plain = 'a', .accented = "\xC3\xA1"},
      {.plain = 'e', .accented = "\xC3\xA9"},
      {.plain = 'i', .accented = "\xC3\xAD"},
      {.plain = 'o', .accented = "\xC3\xB3"},
      {.plain = 'u', .accented = "\xC3\xBA"},
      {.plain = 'c', .accented = "\xC3\xA7"},
      {.plain = 'n', .accented = "\xC3\xB1"},
      {.plain = 'y', .accented = "\xC3\xBD"},
      {.plain = 'A', .accented = "\xC3\x81"},
      {.plain = 'E', .accented = "\xC3\x89"},
      {.plain = 'I', .accented = "\xC3\x8D"},
      {.plain = 'O', .accented = "\xC3\x93"},
      {.plain = 'U', .accented = "\xC3\x9A"},
      {.plain = 'C', .accented = "\xC3\x87"},
      {.plain = 'N', .accented = "\xC3\x91"},
  }};
  std::string result = "[";
  bool in_placeholder = false;
  for (const char character : text) {
    if (character == '{') {
      in_placeholder = true;
    }
    bool mapped = false;
    if (!in_placeholder) {
      for (const Mapping& mapping : kMap) {
        if (mapping.plain == character) {
          absl::StrAppend(&result, mapping.accented);
          mapped = true;
          break;
        }
      }
    }
    if (!mapped) {
      result.push_back(character);
    }
    if (character == '}') {
      in_placeholder = false;
    }
  }
  absl::StrAppend(&result, " ~~]");
  return result;
}

std::string StringCatalogue::Resolve(
    std::string_view key, const std::vector<umbra::ui::TextArg>& args) const {
  const std::string_view text = Template(key);
  if (locale_ == Locale::kEnglish) {
    return Substitute(text, args);
  }
  return Substitute(PseudoAccent(text), args);
}

std::string FormatClock(std::int64_t microseconds, bool with_hours) {
  const std::int64_t total_seconds =
      (microseconds < 0 ? 0 : microseconds) / 1'000'000;
  const std::int64_t hours = total_seconds / 3600;
  const std::int64_t minutes = (total_seconds / 60) % 60;
  const std::int64_t seconds = total_seconds % 60;
  if (with_hours || hours > 0) {
    return absl::StrCat(hours, ":", absl::Dec(minutes, absl::kZeroPad2), ":",
                        absl::Dec(seconds, absl::kZeroPad2));
  }
  return absl::StrCat(total_seconds / 60, ":",
                      absl::Dec(seconds, absl::kZeroPad2));
}

}
