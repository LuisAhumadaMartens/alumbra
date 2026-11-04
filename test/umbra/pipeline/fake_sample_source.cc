#include "fake_sample_source.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/rational.h"
#include "penumbra/interface/shared_bytes.h"
#include "umbra/base/error_detail.h"
#include "umbra/pipeline/sample_source.h"
#include "umbra/player/tracks.h"

namespace umbra::pipeline::test {

void SourceControl::CompleteOpen() {
  for (const auto& stream : description.streams) {
    cursor_ms[stream.info.track_id] = 0;
  }
  sink->OnOpened(description);
}

void SourceControl::FailOpen(base::ErrorDetail detail) const {
  sink->OnOpenFailed(std::move(detail));
}

void SourceControl::Release() {
  held = false;
  const std::vector<Request> pending = std::move(queued);
  queued.clear();
  for (const Request& request : pending) {
    Deliver(request);
  }
}

void SourceControl::Deliver(const Request& request) {
  const int64_t total_ms = description.duration.InMicroseconds() / 1000;
  int64_t& cursor = cursor_ms[request.track_id];
  if (cursor >= total_ms) {
    sink->OnEndOfStream(request.ticket, request.track_id);
    return;
  }
  std::vector<penumbra::EncodedSample> samples;
  while (samples.size() < request.max_samples && cursor < total_ms) {
    penumbra::EncodedSample sample;
    sample.track_id = request.track_id;
    sample.data = penumbra::SharedBytes::Adopt({1, 2, 3, 4});
    sample.pts = {.value = cursor,
                  .time_base = penumbra::Rational::Of(1, 1000)};
    sample.duration = {.value = sample_ms,
                       .time_base = penumbra::Rational::Of(1, 1000)};
    sample.key_frame = true;
    samples.push_back(std::move(sample));
    cursor += sample_ms;
  }
  sink->OnSamples(request.ticket, request.track_id, std::move(samples));
}

void FakeSampleSource::Open(std::shared_ptr<SampleSink> sink) {
  control_->sink = std::move(sink);
  control_->opened = true;
}

void FakeSampleSource::RequestSamples(penumbra::SeekTicket ticket,
                                      uint32_t track_id, size_t max_samples) {
  const SourceControl::Request request{
      .ticket = ticket, .track_id = track_id, .max_samples = max_samples};
  control_->requests.push_back(request);
  if (control_->held) {
    control_->queued.push_back(request);
    return;
  }
  control_->Deliver(request);
}

SeekPoint FakeSampleSource::ResolveSeek(penumbra::MediaTime position,
                                        SeekMode mode,
                                        FastSeekSnap snap) const {
  const int64_t target_ms = position.InMicroseconds() / 1000;
  const int64_t gop_ms = std::max<int64_t>(control_->keyframe_interval_ms, 1);
  const int64_t previous_ms = control_->keyframe_interval_ms == 0
                                  ? target_ms
                                  : (target_ms / gop_ms) * gop_ms;
  int64_t presentation_ms = target_ms;
  if (mode == SeekMode::kFast) {
    presentation_ms = previous_ms;
    if (snap == FastSeekSnap::kNearest &&
        target_ms - previous_ms > gop_ms / 2) {
      presentation_ms += gop_ms;
    }
  }
  if (control_->keyframe_interval_ms > 0 &&
      control_->description.duration.is_finite()) {
    presentation_ms =
        std::min(presentation_ms,
                 control_->description.duration.InMicroseconds() / 1000);
  }
  return {.presentation_time =
              penumbra::MediaTime::FromMilliseconds(presentation_ms),
          .decode_time = penumbra::MediaTime::FromMilliseconds(
              mode == SeekMode::kFast ? presentation_ms : previous_ms)};
}

void FakeSampleSource::Seek(penumbra::SeekTicket ticket,
                            penumbra::MediaTime decode_position) {
  static_cast<void>(ticket);
  control_->seeks.push_back(decode_position);
  for (auto& [track, cursor] : control_->cursor_ms) {
    cursor = decode_position.InMicroseconds() / 1000;
  }
}

bool FakeSampleSource::SelectAudioTrack(std::string_view id) {
  const auto found = std::ranges::find(control_->description.audio_tracks, id,
                                       &umbra::player::AudioTrack::id);
  if (found == control_->description.audio_tracks.end()) {
    return false;
  }
  for (auto& track : control_->description.audio_tracks) {
    track.enabled = track.id == id;
  }
  control_->audio_selections.emplace_back(id);
  return true;
}

bool FakeSampleSource::SelectVideoTrack(std::string_view id) {
  const auto found = std::ranges::find(control_->description.video_tracks, id,
                                       &umbra::player::VideoTrack::id);
  if (found == control_->description.video_tracks.end()) {
    return false;
  }
  for (auto& track : control_->description.video_tracks) {
    track.selected = track.id == id;
  }
  control_->video_selections.emplace_back(id);
  return true;
}

bool FakeSampleSource::ClearVideoTrackSelection() {
  ++control_->video_selection_clears;
  const auto automatic = std::ranges::find_if(
      control_->description.video_tracks,
      [](const umbra::player::VideoTrack& track) {
        return track.kind == umbra::player::VideoTrackKind::kMain;
      });
  for (auto& track : control_->description.video_tracks) {
    track.selected = automatic != control_->description.video_tracks.end() &&
                     track.id == automatic->id;
  }
  return true;
}

std::string FakeSampleSource::SelectedAudioTrackId() const {
  const auto selected = std::ranges::find_if(
      control_->description.audio_tracks,
      [](const umbra::player::AudioTrack& track) { return track.enabled; });
  return selected == control_->description.audio_tracks.end() ? std::string{}
                                                              : selected->id;
}

std::string FakeSampleSource::SelectedVideoTrackId() const {
  const auto selected = std::ranges::find_if(
      control_->description.video_tracks,
      [](const umbra::player::VideoTrack& track) { return track.selected; });
  return selected == control_->description.video_tracks.end() ? std::string{}
                                                              : selected->id;
}

void FakeSampleSource::Close() { control_->closed = true; }

}
