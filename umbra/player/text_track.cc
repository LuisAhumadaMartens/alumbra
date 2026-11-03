#include "umbra/player/text_track.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "umbra/player/events.h"

namespace umbra::player {
namespace {

uint64_t NextTextTrackId() {
  static std::atomic<uint64_t> next_track_id{1};
  return next_track_id.fetch_add(1);
}

size_t TextCueBytes(const TextCue& cue) {
  size_t bytes = sizeof(TextCue) + cue.region.capacity() +
                 cue.ruby_annotation.capacity() +
                 (cue.runs.capacity() * sizeof(StyledTextRun));
  for (const StyledTextRun& run : cue.runs) {
    bytes += sizeof(StyledTextRun) + run.text.capacity() +
             run.voice.capacity() + run.language.capacity();
  }
  return bytes;
}

size_t BitmapCueBytes(const BitmapCue& cue) {
  return sizeof(BitmapCue) + cue.rgba.capacity();
}

struct Listener {
  std::function<void()> callback;
  std::function<void(TextTrackListEventType)> list_callback;
  bool active = true;
};

}

class TextTrack::Impl {
 public:
  Impl(TextTrackKind track_kind, std::string track_label,
       std::string track_language, size_t budget)
      : kind(track_kind),
        label(std::move(track_label)),
        language(std::move(track_language)),
        budget_bytes(budget) {
    id += std::to_string(NextTextTrackId());
  }

  void Notify() const {
    const auto snapshot = listeners;
    for (const auto& listener : snapshot) {
      if (listener->active) {
        listener->callback();
      }
    }
  }

  TextTrackKind kind;
  std::string label;
  std::string language;
  std::string id = "text-track";
  TextTrackMode mode = TextTrackMode::kDisabled;
  size_t budget_bytes;
  size_t bytes = 0;
  double playhead = 0.0;
  std::vector<TextCue> text;
  std::vector<BitmapCue> bitmaps;
  std::vector<uint64_t> active_ids;
  std::vector<std::shared_ptr<Listener>> listeners;
};

TextTrack::TextTrack(TextTrackKind kind, std::string label,
                     std::string language, size_t memory_budget_bytes)
    : impl_(std::make_unique<Impl>(kind, std::move(label), std::move(language),
                                   memory_budget_bytes)) {}
TextTrack::~TextTrack() = default;
TextTrackKind TextTrack::kind() const { return impl_->kind; }
const std::string& TextTrack::label() const { return impl_->label; }
const std::string& TextTrack::language() const { return impl_->language; }
const std::string& TextTrack::id() const { return impl_->id; }
TextTrackMode TextTrack::mode() const { return impl_->mode; }

void TextTrack::set_mode(TextTrackMode mode) {
  if (impl_->mode == mode) {
    return;
  }
  const bool had_active = !impl_->active_ids.empty();
  impl_->mode = mode;
  if (mode == TextTrackMode::kDisabled) {
    impl_->active_ids.clear();
    if (had_active) {
      impl_->Notify();
    }
  } else {
    Update(impl_->playhead);
  }
}

bool TextTrack::AddCue(TextCue cue) {
  if (!std::isfinite(cue.start_time) || !std::isfinite(cue.end_time) ||
      cue.start_time < 0.0 || cue.end_time < cue.start_time) {
    return false;
  }
  if (std::any_of(impl_->text.begin(), impl_->text.end(),
                  [&cue](const TextCue& item) { return item.id == cue.id; }) ||
      std::any_of(
          impl_->bitmaps.begin(), impl_->bitmaps.end(),
          [&cue](const BitmapCue& item) { return item.id == cue.id; })) {
    return false;
  }
  const size_t bytes = TextCueBytes(cue);
  if (bytes > impl_->budget_bytes) {
    return false;
  }
  while (impl_->bytes + bytes > impl_->budget_bytes) {
    auto old = std::find_if(impl_->text.begin(), impl_->text.end(),
                            [this](const TextCue& item) {
                              return item.end_time <= impl_->playhead;
                            });
    if (old != impl_->text.end()) {
      impl_->bytes -= TextCueBytes(*old);
      impl_->text.erase(old);
      continue;
    }
    auto old_bitmap = std::find_if(impl_->bitmaps.begin(), impl_->bitmaps.end(),
                                   [this](const BitmapCue& item) {
                                     return item.end_time <= impl_->playhead;
                                   });
    if (old_bitmap == impl_->bitmaps.end()) {
      return false;
    }
    impl_->bytes -= BitmapCueBytes(*old_bitmap);
    impl_->bitmaps.erase(old_bitmap);
  }
  impl_->bytes += bytes;
  impl_->text.push_back(std::move(cue));
  std::stable_sort(impl_->text.begin(), impl_->text.end(),
                   [](const TextCue& left, const TextCue& right) {
                     if (left.start_time != right.start_time) {
                       return left.start_time < right.start_time;
                     }
                     return left.id < right.id;
                   });
  Update(impl_->playhead);
  return true;
}

bool TextTrack::AddBitmapCue(BitmapCue cue) {
  const bool dimensions_valid =
      cue.width > 0 && cue.height > 0 &&
      cue.width <= std::numeric_limits<size_t>::max() / 4 / cue.height &&
      static_cast<size_t>(cue.width) * cue.height * 4 == cue.rgba.size();
  if (!std::isfinite(cue.start_time) || !std::isfinite(cue.end_time) ||
      cue.start_time < 0.0 || cue.end_time < cue.start_time ||
      !dimensions_valid) {
    return false;
  }
  if (std::any_of(impl_->text.begin(), impl_->text.end(),
                  [&cue](const TextCue& item) { return item.id == cue.id; }) ||
      std::any_of(
          impl_->bitmaps.begin(), impl_->bitmaps.end(),
          [&cue](const BitmapCue& item) { return item.id == cue.id; })) {
    return false;
  }
  const size_t bytes = BitmapCueBytes(cue);
  if (bytes > impl_->budget_bytes) {
    return false;
  }
  while (impl_->bytes + bytes > impl_->budget_bytes) {
    auto old_text = std::find_if(impl_->text.begin(), impl_->text.end(),
                                 [this](const TextCue& item) {
                                   return item.end_time <= impl_->playhead;
                                 });
    if (old_text != impl_->text.end()) {
      impl_->bytes -= TextCueBytes(*old_text);
      impl_->text.erase(old_text);
      continue;
    }
    auto old = std::find_if(impl_->bitmaps.begin(), impl_->bitmaps.end(),
                            [this](const BitmapCue& item) {
                              return item.end_time <= impl_->playhead;
                            });
    if (old == impl_->bitmaps.end()) {
      return false;
    }
    impl_->bytes -= BitmapCueBytes(*old);
    impl_->bitmaps.erase(old);
  }
  impl_->bytes += bytes;
  impl_->bitmaps.push_back(std::move(cue));
  std::stable_sort(impl_->bitmaps.begin(), impl_->bitmaps.end(),
                   [](const BitmapCue& left, const BitmapCue& right) {
                     if (left.start_time != right.start_time) {
                       return left.start_time < right.start_time;
                     }
                     return left.id < right.id;
                   });
  Update(impl_->playhead);
  return true;
}

void TextTrack::RemoveCue(uint64_t id) {
  const auto before = impl_->text.size() + impl_->bitmaps.size();
  std::erase_if(impl_->text, [id](const TextCue& cue) { return cue.id == id; });
  std::erase_if(impl_->bitmaps,
                [id](const BitmapCue& cue) { return cue.id == id; });
  if (before != impl_->text.size() + impl_->bitmaps.size()) {
    impl_->bytes = 0;
    for (const TextCue& cue : impl_->text) {
      impl_->bytes += TextCueBytes(cue);
    }
    for (const BitmapCue& cue : impl_->bitmaps) {
      impl_->bytes += BitmapCueBytes(cue);
    }
    Update(impl_->playhead);
  }
}

void TextTrack::Update(double media_time) {
  if (!std::isfinite(media_time)) {
    return;
  }
  impl_->playhead = media_time;
  const std::vector<uint64_t> old = impl_->active_ids;
  impl_->active_ids.clear();
  if (impl_->mode != TextTrackMode::kDisabled) {
    for (const TextCue& cue : impl_->text) {
      if (cue.start_time <= media_time && media_time < cue.end_time) {
        impl_->active_ids.push_back(cue.id);
      }
    }
  }
  if (old != impl_->active_ids) {
    impl_->Notify();
  }
}

std::vector<TextCue> TextTrack::cues() const { return impl_->text; }
std::vector<TextCue> TextTrack::active_cues() const {
  std::vector<TextCue> result;
  result.reserve(impl_->active_ids.size());
  for (const uint64_t id : impl_->active_ids) {
    const auto found =
        std::find_if(impl_->text.begin(), impl_->text.end(),
                     [id](const TextCue& cue) { return cue.id == id; });
    if (found != impl_->text.end()) {
      result.push_back(*found);
    }
  }
  return result;
}
std::vector<BitmapCue> TextTrack::bitmap_cues() const { return impl_->bitmaps; }
size_t TextTrack::memory_usage_bytes() const { return impl_->bytes; }
Subscription TextTrack::AddCueChangeListener(std::function<void()> listener) {
  auto entry = std::make_shared<Listener>();
  entry->callback = std::move(listener);
  impl_->listeners.push_back(entry);
  return Subscription([this, entry] {
    entry->active = false;
    std::erase(impl_->listeners, entry);
  });
}

class TextTrackList::Impl {
 public:
  std::vector<std::shared_ptr<TextTrack>> tracks;
  std::vector<std::shared_ptr<Listener>> listeners;
  void Notify(TextTrackListEventType event) const {
    const auto snapshot = listeners;
    for (const auto& listener : snapshot) {
      if (listener->active && listener->list_callback) {
        listener->list_callback(event);
      }
    }
  }
};

TextTrackList::TextTrackList() : impl_(std::make_unique<Impl>()) {}
TextTrackList::~TextTrackList() = default;
size_t TextTrackList::length() const { return impl_->tracks.size(); }
std::shared_ptr<TextTrack> TextTrackList::item(size_t index) const {
  return index < impl_->tracks.size() ? impl_->tracks[index] : nullptr;
}
void TextTrackList::Add(std::shared_ptr<TextTrack> track) {
  if (track == nullptr) {
    return;
  }
  impl_->tracks.push_back(std::move(track));
  impl_->Notify(TextTrackListEventType::kAddTrack);
  impl_->Notify(TextTrackListEventType::kChange);
}
bool TextTrackList::Remove(const std::string& id) {
  const auto found =
      std::find_if(impl_->tracks.begin(), impl_->tracks.end(),
                   [&id](const auto& track) { return track->id() == id; });
  if (found == impl_->tracks.end()) {
    return false;
  }
  impl_->tracks.erase(found);
  impl_->Notify(TextTrackListEventType::kRemoveTrack);
  impl_->Notify(TextTrackListEventType::kChange);
  return true;
}
Subscription TextTrackList::AddChangeListener(std::function<void()> listener) {
  auto entry = std::make_shared<Listener>();
  entry->list_callback =
      [listener = std::move(listener)](TextTrackListEventType event) {
        if (event == TextTrackListEventType::kChange) {
          listener();
        }
      };
  impl_->listeners.push_back(entry);
  return Subscription([this, entry] {
    entry->active = false;
    std::erase(impl_->listeners, entry);
  });
}

Subscription TextTrackList::AddEventListener(
    std::function<void(TextTrackListEventType)> listener) {
  auto entry = std::make_shared<Listener>();
  entry->list_callback = std::move(listener);
  impl_->listeners.push_back(entry);
  return Subscription([this, entry] {
    entry->active = false;
    std::erase(impl_->listeners, entry);
  });
}

}
