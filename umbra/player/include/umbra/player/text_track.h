#ifndef UMBRA_PLAYER_TEXT_TRACK_H_
#define UMBRA_PLAYER_TEXT_TRACK_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "umbra/player/events.h"

namespace umbra::player {

enum class TextTrackKind : uint8_t {
  kSubtitles,
  kCaptions,
  kDescriptions,
  kChapters,
  kMetadata
};
enum class TextTrackMode : uint8_t { kDisabled, kHidden, kShowing };
enum class TextTrackListEventType : uint8_t {
  kAddTrack,
  kRemoveTrack,
  kChange
};

struct StyledTextRun {
  std::string text;
  std::string voice;
  std::string language;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  std::string color;
};

struct TextCue {
  uint64_t id = 0;
  double start_time = 0.0;
  double end_time = 0.0;
  std::vector<StyledTextRun> runs;
  std::string region;
  double line = 0.0;
  double position = 50.0;
  double size = 100.0;
  bool vertical = false;
  bool right_to_left = false;
  std::string ruby_annotation;
};

struct BitmapCue {
  uint64_t id = 0;
  double start_time = 0.0;
  double end_time = 0.0;
  std::vector<uint8_t> rgba;
  uint32_t width = 0;
  uint32_t height = 0;
};

class TextTrack {
 public:
  TextTrack(TextTrackKind kind, std::string label, std::string language,
            size_t memory_budget_bytes);
  TextTrack(const TextTrack&) = delete;
  TextTrack& operator=(const TextTrack&) = delete;
  ~TextTrack();

  [[nodiscard]] TextTrackKind kind() const;
  [[nodiscard]] const std::string& label() const;
  [[nodiscard]] const std::string& language() const;
  [[nodiscard]] const std::string& id() const;
  [[nodiscard]] TextTrackMode mode() const;
  void set_mode(TextTrackMode mode);
  bool LoadSubRip(std::string bytes, std::string fallback_encoding,
                  bool auto_detect);
  bool SetEncodingOverride(std::string encoding);
  [[nodiscard]] const std::string& TextEncoding() const;
  [[nodiscard]] double TextEncodingConfidence() const;
  bool AddCue(TextCue cue);
  bool AddBitmapCue(BitmapCue cue);
  void RemoveCue(uint64_t id);
  void Update(double media_time);
  [[nodiscard]] std::vector<TextCue> cues() const;
  [[nodiscard]] std::vector<TextCue> active_cues() const;
  [[nodiscard]] std::vector<BitmapCue> bitmap_cues() const;
  [[nodiscard]] size_t memory_usage_bytes() const;
  Subscription AddCueChangeListener(std::function<void()> listener);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class TextTrackList {
 public:
  TextTrackList();
  TextTrackList(const TextTrackList&) = delete;
  TextTrackList& operator=(const TextTrackList&) = delete;
  ~TextTrackList();
  [[nodiscard]] size_t length() const;
  [[nodiscard]] std::shared_ptr<TextTrack> item(size_t index) const;
  void Add(std::shared_ptr<TextTrack> track);
  bool Remove(const std::string& id);
  Subscription AddChangeListener(std::function<void()> listener);
  Subscription AddEventListener(
      std::function<void(TextTrackListEventType)> listener);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
