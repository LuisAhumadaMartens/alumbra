#ifndef UMBRA_DEMUX_TS_SEEK_MAP_H_
#define UMBRA_DEMUX_TS_SEEK_MAP_H_

#include <cstdint>
#include <optional>

#include "penumbra/interface/media_time.h"
#include "umbra/demux/demuxer.h"

namespace umbra::demux {

class TsSeekMap final : public SeekMap {
 public:
  TsSeekMap() = default;
  TsSeekMap(penumbra::MediaTime start, penumbra::MediaTime end,
            uint64_t start_offset, uint64_t size)
      : seekable_(true),
        start_(start),
        end_(end),
        start_offset_(start_offset),
        size_(size) {}

  [[nodiscard]] bool IsSeekable() const override { return seekable_; }
  [[nodiscard]] penumbra::TimeDelta Duration() const override;
  [[nodiscard]] std::optional<SeekPoint> SeekPointFor(
      penumbra::MediaTime target) const override;

 private:
  bool seekable_ = false;
  penumbra::MediaTime start_;
  penumbra::MediaTime end_;
  uint64_t start_offset_ = 0;
  uint64_t size_ = 0;
};

}

#endif
