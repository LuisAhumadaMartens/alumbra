#ifndef UMBRA_MANIFEST_IDS_H_
#define UMBRA_MANIFEST_IDS_H_

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace umbra::manifest {

inline constexpr std::size_t kMaxIdBytes = 1024;

template <typename Tag>
class StableId {
 public:
  StableId() = default;
  explicit StableId(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] const std::string& Value() const { return value_; }
  [[nodiscard]] bool empty() const { return value_.empty(); }

  friend bool operator==(const StableId&, const StableId&) = default;
  friend auto operator<=>(const StableId&, const StableId&) = default;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const StableId& id) {
    sink.Append(id.value_);
  }

 private:
  std::string value_;
};

struct PeriodTag {};
struct SwitchingSetTag {};
struct TrackTag {};

using PeriodId = StableId<PeriodTag>;
using SwitchingSetId = StableId<SwitchingSetTag>;
using TrackId = StableId<TrackTag>;

enum class RenditionType : std::uint8_t {
  kAudio,
  kSubtitles,
  kClosedCaptions,
};

struct HlsRenditionKey {
  RenditionType type = RenditionType::kAudio;
  std::string group_id;
  std::string name;
  std::string language;
  std::string characteristics;
};

struct HlsVariantKey {
  std::int64_t bandwidth = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::string frame_rate;
  std::string video_range;
  std::string codecs;
  std::uint32_t ordinal = 0;
};

[[nodiscard]] std::string EncodeKeyPart(std::string_view part);

[[nodiscard]] PeriodId DashPeriodId(std::string_view period_id);
[[nodiscard]] PeriodId DashPeriodIdByIndex(std::size_t index);
[[nodiscard]] SwitchingSetId DashSwitchingSetId(
    std::string_view adaptation_set_id);
[[nodiscard]] TrackId DashTrackId(std::string_view adaptation_set_id,
                                  std::string_view representation_id);

[[nodiscard]] PeriodId HlsPeriodId(std::int64_t discontinuity_sequence);
[[nodiscard]] SwitchingSetId HlsVideoSwitchingSetId();
[[nodiscard]] TrackId HlsVariantTrackId(const HlsVariantKey& key);
[[nodiscard]] SwitchingSetId HlsRenditionSwitchingSetId(
    const HlsRenditionKey& key);
[[nodiscard]] TrackId HlsRenditionTrackId(const HlsRenditionKey& key);
[[nodiscard]] TrackId HlsTrickPlayTrackId(const HlsVariantKey& key);

}

#endif
