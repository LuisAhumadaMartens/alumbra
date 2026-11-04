#ifndef UMBRA_PIPELINE_SAMPLE_SOURCE_H_
#define UMBRA_PIPELINE_SAMPLE_SOURCE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "umbra/base/error_detail.h"
#include "umbra/player/tracks.h"

namespace umbra::pipeline {

enum class SeekMode : uint8_t { kAccurate, kFast };

enum class FastSeekSnap : uint8_t { kPrevious, kNearest };

struct SeekPoint {
  penumbra::MediaTime presentation_time;
  penumbra::MediaTime decode_time;
};

struct SeekableRange {
  penumbra::MediaTime start;
  penumbra::MediaTime end;
};

struct SourceDescription {
  std::vector<penumbra::StreamConfig> streams;
  std::vector<umbra::player::AudioTrack> audio_tracks;
  std::vector<umbra::player::VideoTrack> video_tracks;
  penumbra::MediaTime duration = penumbra::MediaTime::Unknown();
  std::optional<SeekableRange> seekable_range;
};

class SampleSink {
 public:
  virtual ~SampleSink() = default;
  virtual void OnOpened(SourceDescription description) = 0;
  virtual void OnOpenFailed(base::ErrorDetail detail) = 0;
  virtual void OnDurationChanged(penumbra::MediaTime duration) = 0;
  virtual void OnSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                         std::vector<penumbra::EncodedSample> samples) = 0;
  virtual void OnEndOfStream(penumbra::SeekTicket ticket,
                             uint32_t track_id) = 0;
  virtual void OnSourceError(base::ErrorDetail detail) = 0;
};

class SampleSource {
 public:
  virtual ~SampleSource() = default;
  virtual void Open(std::shared_ptr<SampleSink> sink) = 0;
  virtual void RequestSamples(penumbra::SeekTicket ticket, uint32_t track_id,
                              size_t max_samples) = 0;
  [[nodiscard]] virtual SeekPoint ResolveSeek(penumbra::MediaTime position,
                                              SeekMode mode,
                                              FastSeekSnap snap) const = 0;
  virtual void Seek(penumbra::SeekTicket ticket,
                    penumbra::MediaTime decode_position) = 0;
  [[nodiscard]] virtual bool SelectAudioTrack(std::string_view id) {
    static_cast<void>(id);
    return false;
  }
  [[nodiscard]] virtual bool SelectVideoTrack(std::string_view id) {
    static_cast<void>(id);
    return false;
  }
  [[nodiscard]] virtual bool ClearVideoTrackSelection() { return false; }
  [[nodiscard]] virtual std::string SelectedAudioTrackId() const { return {}; }
  [[nodiscard]] virtual std::string SelectedVideoTrackId() const { return {}; }
  virtual void Close() = 0;
};

}

#endif
