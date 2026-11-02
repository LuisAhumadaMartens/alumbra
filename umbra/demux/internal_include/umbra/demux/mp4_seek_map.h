#ifndef UMBRA_DEMUX_MP4_SEEK_MAP_H_
#define UMBRA_DEMUX_MP4_SEEK_MAP_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"
#include "umbra/demux/demuxer.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {

[[nodiscard]] size_t FindSeekIndex(const SampleList& samples, int64_t shift,
                                   int64_t target_ticks);

struct SeekTrack {
  std::shared_ptr<const SampleList> samples;
  int64_t shift = 0;
  penumbra::Rational time_base;
  bool reference = false;
};

class ProgressiveSeekMap final : public SeekMap {
 public:
  ProgressiveSeekMap(std::vector<SeekTrack> tracks,
                     penumbra::TimeDelta duration)
      : tracks_(std::move(tracks)), duration_(duration) {}

  [[nodiscard]] bool IsSeekable() const override { return true; }
  [[nodiscard]] penumbra::TimeDelta Duration() const override {
    return duration_;
  }
  [[nodiscard]] std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const override;

 private:
  std::vector<SeekTrack> tracks_;
  penumbra::TimeDelta duration_;
};

class IndexedSeekMap final : public SeekMap {
 public:
  IndexedSeekMap(std::vector<SeekPoint> points, penumbra::TimeDelta duration)
      : points_(std::move(points)), duration_(duration) {}

  [[nodiscard]] bool IsSeekable() const override { return !points_.empty(); }
  [[nodiscard]] penumbra::TimeDelta Duration() const override {
    return duration_;
  }
  [[nodiscard]] std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const override;

 private:
  std::vector<SeekPoint> points_;
  penumbra::TimeDelta duration_;
};

}

#endif
