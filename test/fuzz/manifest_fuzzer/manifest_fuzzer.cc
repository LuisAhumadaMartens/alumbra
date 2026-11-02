#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "fuzz_target.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/stream_info.h"
#include "umbra/manifest/ids.h"
#include "umbra/manifest/presentation.h"
#include "umbra/manifest/refresh.h"
#include "umbra/manifest/segment.h"
#include "umbra/manifest/segment_index.h"
#include "umbra/manifest/url_template.h"
#include "umbra/manifest/validate.h"

namespace umbra::manifest {
namespace {

void Check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

void CheckError(const absl::Status& status) {
  Check(status.code() == absl::StatusCode::kInvalidArgument);
  Check(status.message().find("ALUMBRA-2001") != std::string_view::npos);
}

class Cursor {
 public:
  explicit Cursor(std::span<const std::uint8_t> data) : data_(data) {}

  std::uint8_t Next() {
    if (position_ >= data_.size()) {
      return 0;
    }
    return data_[position_++];
  }
  std::int64_t Small() { return static_cast<std::int64_t>(Next() % 16); }
  std::int64_t Signed() { return static_cast<std::int64_t>(Next() % 24) - 4; }
  bool Flag() { return (Next() & 1U) != 0; }
  bool Rare() { return Next() % 8 == 0; }
  std::string Text() {
    std::string text;
    const std::size_t length = Next() % 12;
    for (std::size_t i = 0; i < length; ++i) {
      text.push_back(static_cast<char>(Next()));
    }
    return text;
  }
  std::string_view Rest() {
    const std::span<const std::uint8_t> rest = data_.subspan(position_);
    position_ = data_.size();
    return {reinterpret_cast<const char*>(rest.data()), rest.size()};
  }

 private:
  std::span<const std::uint8_t> data_;
  std::size_t position_ = 0;
};

penumbra::MediaTime Seconds(std::int64_t seconds) {
  return penumbra::MediaTime::FromSeconds(seconds);
}

penumbra::TimeDelta Delta(std::int64_t seconds) {
  return penumbra::TimeDelta::FromSeconds(seconds);
}

InitSection MakeInit(std::string uri) {
  InitSection init;
  init.uri = std::move(uri);
  return init;
}

Segment BuildSegment(Cursor& cursor, std::int64_t sequence,
                     std::int64_t clock) {
  Segment segment;
  segment.sequence_number = cursor.Rare() ? cursor.Small() : sequence;
  segment.start = Seconds(clock + (cursor.Signed() / 8));
  segment.duration = Delta(cursor.Signed() / 4);
  segment.uri = cursor.Flag() ? "https://x.test/s.m4s" : cursor.Text();
  segment.gap = cursor.Rare();
  if (cursor.Flag()) {
    segment.init_index = static_cast<std::uint32_t>(cursor.Small());
  }
  if (cursor.Rare()) {
    ByteRange range;
    range.offset = cursor.Signed();
    range.length = cursor.Signed();
    segment.byte_range = range;
  }
  segment.discontinuity_sequence = cursor.Rare() ? cursor.Small() : 0;
  return segment;
}

std::shared_ptr<const SegmentIndex> BuildExplicitIndex(Cursor& cursor) {
  ExplicitSegmentIndexParameters parameters;
  parameters.first_sequence_number = cursor.Small();
  std::int64_t sequence = parameters.first_sequence_number;
  std::int64_t clock = cursor.Small();
  const std::size_t count = cursor.Next() % 8;
  for (std::size_t i = 0; i < count; ++i) {
    Segment segment = BuildSegment(cursor, sequence, clock);
    clock = (segment.start.InMicroseconds() / 1'000'000) +
            (segment.duration.InMicroseconds() / 1'000'000);
    ++sequence;
    parameters.segments.push_back(std::move(segment));
  }
  if (cursor.Flag()) {
    parameters.init_sections.push_back(
        MakeInit(cursor.Flag() ? "https://x.test/i.mp4" : cursor.Text()));
  }
  absl::StatusOr<std::shared_ptr<const ExplicitSegmentIndex>> index =
      ExplicitSegmentIndex::Create(std::move(parameters));
  if (!index.ok()) {
    CheckError(index.status());
    return nullptr;
  }
  return *index;
}

std::shared_ptr<const SegmentIndex> BuildTemplateIndex(Cursor& cursor) {
  constexpr std::array<std::string_view, 5> kTemplates = {
      "s$Number$.m4s", "t$Time%08d$.m4s", "$RepresentationID$/$Number$-$Time$",
      "plain.m4s", "$Number%0d$"};
  constexpr std::array<std::int64_t, 6> kBases = {
      1, 1000, 90'000, 10'000'000, 1'000'000'000, 0};
  TemplateSegmentIndexParameters parameters;
  parameters.media_template = std::string(kTemplates.at(cursor.Next() % 5));
  parameters.representation_id = cursor.Text();
  parameters.bandwidth = cursor.Signed() * 1000;
  parameters.first_sequence_number = cursor.Signed();
  const std::int64_t base = kBases.at(cursor.Next() % 6);
  if (base != 0) {
    parameters.time_base = *penumbra::Rational::Create(1, base);
  }
  parameters.presentation_time_offset_ticks = cursor.Signed();
  parameters.period_start = Seconds(cursor.Signed());
  if (cursor.Flag()) {
    parameters.init_section = MakeInit("https://x.test/i.mp4");
  }
  const std::size_t runs = cursor.Next() % 4;
  for (std::size_t i = 0; i < runs; ++i) {
    TimelineRun run;
    run.start_ticks = cursor.Signed() * std::max<std::int64_t>(base, 1);
    run.duration_ticks = cursor.Small() * std::max<std::int64_t>(base / 2, 1);
    run.count = static_cast<std::int64_t>(cursor.Next());
    parameters.runs.push_back(run);
  }
  absl::StatusOr<std::shared_ptr<const TemplateSegmentIndex>> index =
      TemplateSegmentIndex::Create(std::move(parameters));
  if (!index.ok()) {
    CheckError(index.status());
    return nullptr;
  }
  return *index;
}

std::shared_ptr<const SegmentIndex> BuildIndex(Cursor& cursor) {
  if (cursor.Next() % 3 == 0) {
    return nullptr;
  }
  return cursor.Flag() ? BuildExplicitIndex(cursor)
                       : BuildTemplateIndex(cursor);
}

void BuildFormat(Cursor& cursor, Track& track) {
  const std::uint8_t kind = cursor.Next() % 3;
  if (kind == 0) {
    VideoTrackFormat video;
    video.width = static_cast<std::int32_t>(cursor.Small()) * 160;
    video.height = static_cast<std::int32_t>(cursor.Small()) * 90;
    if (cursor.Flag()) {
      video.frame_rate =
          *penumbra::Rational::Create(cursor.Signed() * 10, cursor.Small() + 1);
    }
    video.bit_depth = static_cast<std::int32_t>(cursor.Small());
    track.format = video;
  } else if (kind == 1) {
    AudioTrackFormat audio;
    audio.sample_rate = static_cast<std::int32_t>(cursor.Small()) * 4000;
    audio.channels.channel_count = static_cast<std::int32_t>(cursor.Signed());
    track.format = audio;
  } else {
    track.format = TextTrackFormat();
  }
}

std::shared_ptr<const Track> BuildTrack(Cursor& cursor) {
  Track track;
  track.id =
      TrackId(std::string(1, static_cast<char>('a' + (cursor.Next() % 4))));
  track.bandwidth_bps = cursor.Signed() * 100'000;
  BuildFormat(cursor, track);
  if (cursor.Flag()) {
    track.allowed_audio.emplace_back(
        std::string(1, static_cast<char>('A' + (cursor.Next() % 3))));
  }
  const std::shared_ptr<const SegmentIndex> index = BuildIndex(cursor);
  if (index != nullptr) {
    track.segments = SegmentIndexProvider::Resolved(index);
  } else if (cursor.Flag()) {
    IndexLocation location;
    location.uri = "https://x.test/media.m3u8";
    track.segments = SegmentIndexProvider::Deferred(location);
  }
  return Freeze(std::move(track));
}

penumbra::StreamType StreamTypeOf(std::uint8_t kind) {
  switch (kind % 3) {
    case 0:
      return penumbra::StreamType::kVideo;
    case 1:
      return penumbra::StreamType::kAudio;
    default:
      return penumbra::StreamType::kText;
  }
}

std::shared_ptr<const SwitchingSet> BuildSet(Cursor& cursor) {
  SwitchingSet set;
  set.id = SwitchingSetId(
      std::string(1, static_cast<char>('A' + (cursor.Next() % 3))));
  set.type = StreamTypeOf(cursor.Next());
  set.language = cursor.Text();
  const std::size_t tracks = cursor.Next() % 3;
  for (std::size_t k = 0; k < tracks; ++k) {
    set.tracks.push_back(BuildTrack(cursor));
  }
  return Freeze(std::move(set));
}

std::shared_ptr<const Period> BuildPeriod(Cursor& cursor) {
  Period period;
  period.id =
      PeriodId(std::string(1, static_cast<char>('p' + (cursor.Next() % 3))));
  period.start = Seconds(cursor.Signed() * 4);
  period.duration = cursor.Next() % 5 == 0 ? penumbra::TimeDelta::Unknown()
                                           : Delta(cursor.Signed() * 2);
  const std::size_t sets = cursor.Next() % 3;
  for (std::size_t j = 0; j < sets; ++j) {
    period.switching_sets.push_back(BuildSet(cursor));
  }
  if (cursor.Next() % 4 == 0) {
    period.trick_play.push_back(BuildTrack(cursor));
  }
  return Freeze(std::move(period));
}

Presentation BuildPresentationDraft(Cursor& cursor) {
  Presentation presentation;
  presentation.protocol = cursor.Flag() ? Protocol::kHls : Protocol::kDash;
  presentation.type =
      cursor.Flag() ? PresentationType::kDynamic : PresentationType::kStatic;
  presentation.duration =
      cursor.Flag() ? penumbra::TimeDelta::Unknown() : Delta(cursor.Small());
  if (cursor.Next() % 4 != 0) {
    LiveInfo live;
    live.window_start = Seconds(cursor.Small());
    live.window_end = Seconds(cursor.Small());
    if (cursor.Flag()) {
      live.hold_back = Delta(cursor.Signed());
    }
    presentation.live = live;
  }
  const std::size_t periods = cursor.Next() % 3;
  for (std::size_t i = 0; i < periods; ++i) {
    presentation.periods.push_back(BuildPeriod(cursor));
  }
  if (cursor.Next() % 4 == 0) {
    AdBreak ad_break;
    ad_break.id = cursor.Text();
    ad_break.start = Seconds(cursor.Signed());
    ad_break.duration = Delta(cursor.Signed());
    presentation.ad_breaks.push_back(std::move(ad_break));
  }
  return presentation;
}

void CheckIndex(const SegmentIndex& index) {
  if (index.empty()) {
    Check(!index.FindByTime(Seconds(0)).has_value());
    return;
  }
  const std::size_t last = index.size() - 1;
  for (const std::size_t position : {std::size_t{0}, last / 2, last}) {
    const Segment segment = index.At(position);
    Check(segment.start == index.StartOf(position));
    Check(segment.End() == index.EndOf(position));
    Check(segment.duration.InMicroseconds() > 0);
    Check(index.FindByTime(segment.start) == position);
    Check(index.FindBySequence(segment.sequence_number) == position);
    Check(!index.FindByTime(index.End()).has_value());
  }
}

void CheckValid(const Presentation& presentation) {
  const absl::StatusOr<std::shared_ptr<const Presentation>> built =
      BuildPresentation(presentation);
  Check(built.ok());
  Check(Equivalent(**built, **built));
  for (const std::shared_ptr<const Period>& period : (*built)->periods) {
    Check((*built)->FindPeriod(period->start) == period.get());
    for (const std::shared_ptr<const SwitchingSet>& set :
         period->switching_sets) {
      for (const std::shared_ptr<const Track>& track : set->tracks) {
        if (track->segments.Index() != nullptr) {
          CheckIndex(*track->segments.Index());
        }
      }
    }
  }
  const absl::StatusOr<std::shared_ptr<const Presentation>> same =
      RefreshPresentation(*built, **built);
  Check(same.ok() && same->get() == built->get());
}

void FuzzPresentations(Cursor& cursor) {
  const Presentation first = BuildPresentationDraft(cursor);
  const absl::Status status = ValidatePresentation(first);
  if (!status.ok()) {
    CheckError(status);
    return;
  }
  CheckValid(first);
  const absl::StatusOr<std::shared_ptr<const Presentation>> built =
      BuildPresentation(first);
  Check(built.ok());
  Presentation second = BuildPresentationDraft(cursor);
  const absl::StatusOr<std::shared_ptr<const Presentation>> refreshed =
      RefreshPresentation(*built, std::move(second));
  if (!refreshed.ok()) {
    CheckError(refreshed.status());
    return;
  }
  Check(ValidatePresentation(**refreshed).ok());
}

void FuzzTemplate(std::string_view text) {
  const absl::StatusOr<UrlTemplate> parsed = UrlTemplate::Parse(text);
  if (!parsed.ok()) {
    CheckError(parsed.status());
    return;
  }
  Check(parsed->UsesNumber() || parsed->UsesTime());
  TemplateValues values;
  values.number = -7;
  values.time = 123'456'789;
  values.bandwidth = 5;
  values.representation_id = "rep";
  const std::string expanded = parsed->Expand(values);
  Check(!expanded.empty() || parsed->Text().empty());
}

void FuzzKeyParts(std::string_view text) {
  const std::string encoded = EncodeKeyPart(text);
  Check(encoded.find('/') == std::string::npos);
  Check(EncodeKeyPart(text) == encoded);
  Check(DashTrackId(text, text).Value().size() >= encoded.size());
}

}
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::span<const std::uint8_t> input(data, size);
  umbra::manifest::Cursor cursor(input.subspan(1));
  switch (input.front() % 3) {
    case 0:
      umbra::manifest::FuzzPresentations(cursor);
      break;
    case 1:
      umbra::manifest::FuzzTemplate(cursor.Rest());
      break;
    default:
      umbra::manifest::FuzzKeyParts(cursor.Rest());
      break;
  }
  return 0;
}
