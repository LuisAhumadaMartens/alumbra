#ifndef UMBRA_DEMUX_MKV_SEEK_MAP_H_
#define UMBRA_DEMUX_MKV_SEEK_MAP_H_

#include <optional>
#include <utility>
#include <vector>

#include "penumbra/interface/media_time.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux {

class MatroskaSeekMap final : public SeekMap {
 public:
  MatroskaSeekMap(std::vector<SeekPoint> points, penumbra::TimeDelta duration,
                  penumbra::TimeDelta pre_roll)
      : points_(std::move(points)), duration_(duration), pre_roll_(pre_roll) {}

  [[nodiscard]] bool IsSeekable() const override { return !points_.empty(); }
  [[nodiscard]] penumbra::TimeDelta Duration() const override {
    return duration_;
  }
  [[nodiscard]] std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const override;

 private:
  std::vector<SeekPoint> points_;
  penumbra::TimeDelta duration_;
  penumbra::TimeDelta pre_roll_;
};

}

#endif
