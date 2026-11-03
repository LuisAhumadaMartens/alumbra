#ifndef UMBRA_PIPELINE_BUFFER_MANAGER_H_
#define UMBRA_PIPELINE_BUFFER_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include "penumbra/interface/encoded_sample.h"
#include "penumbra/interface/media_time.h"
#include "penumbra/interface/port_types.h"
#include "penumbra/interface/stream_info.h"

namespace umbra::pipeline {

class BufferManager {
 public:
  struct Config {
    std::int64_t forward_goal_ms = 30000;
    std::int64_t back_goal_ms = 10000;
    std::int64_t start_threshold_ms = 1000;
    std::int64_t rebuffer_threshold_ms = 4000;
    std::uint64_t video_max_bytes = std::uint64_t{128} * 1024U * 1024U;
    std::uint64_t audio_max_bytes = std::uint64_t{16} * 1024U * 1024U;
  };

  struct Accounting {
    std::uint64_t audio_bytes = 0;
    std::uint64_t video_bytes = 0;
  };

  BufferManager();
  explicit BufferManager(Config config);

  [[nodiscard]] static bool ValidateConfig(const Config& config);
  [[nodiscard]] bool SetConfig(Config config);
  void SetStreams(const std::vector<penumbra::StreamConfig>& streams);
  void SetPortBudgets(penumbra::BufferBudgets budgets);
  void SetMemoryPressure(penumbra::MemoryPressure pressure);
  void Clear();
  void RecordSamples(const std::vector<penumbra::EncodedSample>& samples);
  [[nodiscard]] std::uint64_t EvictBehind(penumbra::MediaTime position);
  [[nodiscard]] bool CanFetch(std::uint32_t track_id,
                              penumbra::MediaTime position,
                              penumbra::MediaTime buffered_end,
                              std::int64_t next_segment_ms) const;
  [[nodiscard]] Accounting GetAccounting() const;
  [[nodiscard]] const Config& GetConfig() const { return config_; }
  [[nodiscard]] std::int64_t EffectiveForwardGoalMs() const;

 private:
  struct SampleRecord {
    penumbra::MediaTime start;
    penumbra::MediaTime end;
    std::uint64_t bytes = 0;
    bool key_frame = false;
  };

  struct TrackLedger {
    penumbra::StreamType type = penumbra::StreamType::kVideo;
    std::deque<SampleRecord> samples;
    std::uint64_t bytes = 0;
    std::uint64_t bytes_per_second = 0;
  };

  [[nodiscard]] std::uint64_t BudgetFor(penumbra::StreamType type) const;
  [[nodiscard]] std::uint64_t BytesFor(penumbra::StreamType type) const;
  [[nodiscard]] std::uint64_t EvictBackBuffer(penumbra::MediaTime position);
  [[nodiscard]] std::uint64_t EvictForward(penumbra::StreamType type,
                                           penumbra::MediaTime protected_end);
  [[nodiscard]] static std::uint64_t ProjectedBytes(const TrackLedger& track,
                                                    std::int64_t duration_ms);
  Config config_;
  std::optional<penumbra::MemoryPressure> pressure_;
  penumbra::BufferBudgets port_budgets_;
  std::map<std::uint32_t, TrackLedger> tracks_;
};

}

#endif
