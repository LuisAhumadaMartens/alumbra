#ifndef UMBRA_DEMUX_EBML_H_
#define UMBRA_DEMUX_EBML_H_

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace umbra::demux {

using EbmlBytes = std::span<const uint8_t>;

inline constexpr uint64_t kEbmlUnknownSize =
    std::numeric_limits<uint64_t>::max();
inline constexpr size_t kMaxEbmlIdBytes = 4;
inline constexpr size_t kMaxEbmlSizeBytes = 8;
inline constexpr size_t kMaxEbmlHeaderBytes =
    kMaxEbmlIdBytes + kMaxEbmlSizeBytes;
inline constexpr size_t kMaxEbmlStringBytes = 4096;

struct EbmlHeader {
  uint32_t id = 0;
  uint64_t size = 0;
  size_t header_size = 0;

  [[nodiscard]] bool UnknownSize() const { return size == kEbmlUnknownSize; }
};

struct EbmlElement {
  uint32_t id = 0;
  EbmlBytes payload;
};

struct Vint {
  uint64_t value = 0;
  size_t length = 0;
  bool all_ones = false;
};

[[nodiscard]] absl::StatusOr<size_t> EbmlHeaderBytes(EbmlBytes prefix);

[[nodiscard]] absl::StatusOr<EbmlHeader> ParseEbmlHeader(EbmlBytes prefix);

[[nodiscard]] absl::StatusOr<Vint> ParseVint(EbmlBytes data);

class EbmlReader {
 public:
  explicit EbmlReader(EbmlBytes data) : data_(data) {}

  [[nodiscard]] bool AtEnd() const { return position_ >= data_.size(); }
  [[nodiscard]] absl::StatusOr<EbmlElement> Next();

 private:
  EbmlBytes data_;
  size_t position_ = 0;
};

[[nodiscard]] absl::StatusOr<uint64_t> ReadEbmlUnsigned(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<int64_t> ReadEbmlSigned(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<double> ReadEbmlFloat(EbmlBytes payload);
[[nodiscard]] absl::StatusOr<std::string> ReadEbmlString(EbmlBytes payload);

inline constexpr uint32_t kIdEbml = 0x1A45DFA3;
inline constexpr uint32_t kIdEbmlReadVersion = 0x42F7;
inline constexpr uint32_t kIdEbmlMaxIdLength = 0x42F2;
inline constexpr uint32_t kIdEbmlMaxSizeLength = 0x42F3;
inline constexpr uint32_t kIdDocType = 0x4282;
inline constexpr uint32_t kIdDocTypeReadVersion = 0x4285;

inline constexpr uint32_t kIdSegment = 0x18538067;
inline constexpr uint32_t kIdSeekHead = 0x114D9B74;
inline constexpr uint32_t kIdSeek = 0x4DBB;
inline constexpr uint32_t kIdSeekId = 0x53AB;
inline constexpr uint32_t kIdSeekPosition = 0x53AC;
inline constexpr uint32_t kIdInfo = 0x1549A966;
inline constexpr uint32_t kIdTimestampScale = 0x2AD7B1;
inline constexpr uint32_t kIdDuration = 0x4489;
inline constexpr uint32_t kIdTracks = 0x1654AE6B;
inline constexpr uint32_t kIdCluster = 0x1F43B675;
inline constexpr uint32_t kIdCues = 0x1C53BB6B;
inline constexpr uint32_t kIdTags = 0x1254C367;
inline constexpr uint32_t kIdChapters = 0x1043A770;
inline constexpr uint32_t kIdAttachments = 0x1941A469;

inline constexpr uint32_t kIdTrackEntry = 0xAE;
inline constexpr uint32_t kIdTrackNumber = 0xD7;
inline constexpr uint32_t kIdTrackType = 0x83;
inline constexpr uint32_t kIdFlagLacing = 0x9C;
inline constexpr uint32_t kIdDefaultDuration = 0x23E383;
inline constexpr uint32_t kIdCodecId = 0x86;
inline constexpr uint32_t kIdCodecPrivate = 0x63A2;
inline constexpr uint32_t kIdCodecDelay = 0x56AA;
inline constexpr uint32_t kIdSeekPreRoll = 0x56BB;
inline constexpr uint32_t kIdLanguage = 0x22B59C;
inline constexpr uint32_t kIdLanguageBcp47 = 0x22B59D;
inline constexpr uint32_t kIdVideo = 0xE0;
inline constexpr uint32_t kIdPixelWidth = 0xB0;
inline constexpr uint32_t kIdPixelHeight = 0xBA;
inline constexpr uint32_t kIdDisplayWidth = 0x54B0;
inline constexpr uint32_t kIdDisplayHeight = 0x54BA;
inline constexpr uint32_t kIdDisplayUnit = 0x54B2;
inline constexpr uint32_t kIdColour = 0x55B0;
inline constexpr uint32_t kIdMatrixCoefficients = 0x55B1;
inline constexpr uint32_t kIdRange = 0x55B9;
inline constexpr uint32_t kIdTransferCharacteristics = 0x55BA;
inline constexpr uint32_t kIdPrimaries = 0x55BB;
inline constexpr uint32_t kIdAudio = 0xE1;
inline constexpr uint32_t kIdSamplingFrequency = 0xB5;
inline constexpr uint32_t kIdChannels = 0x9F;
inline constexpr uint32_t kIdContentEncodings = 0x6D80;
inline constexpr uint32_t kIdContentEncoding = 0x6240;
inline constexpr uint32_t kIdContentEncodingScope = 0x5032;
inline constexpr uint32_t kIdContentEncodingType = 0x5033;
inline constexpr uint32_t kIdContentCompression = 0x5034;
inline constexpr uint32_t kIdContentCompAlgo = 0x4254;
inline constexpr uint32_t kIdContentCompSettings = 0x4255;

inline constexpr uint32_t kIdClusterTimestamp = 0xE7;
inline constexpr uint32_t kIdSimpleBlock = 0xA3;
inline constexpr uint32_t kIdBlockGroup = 0xA0;
inline constexpr uint32_t kIdBlock = 0xA1;
inline constexpr uint32_t kIdBlockDuration = 0x9B;
inline constexpr uint32_t kIdReferenceBlock = 0xFB;
inline constexpr uint32_t kIdDiscardPadding = 0x75A2;

inline constexpr uint32_t kIdCuePoint = 0xBB;
inline constexpr uint32_t kIdCueTime = 0xB3;
inline constexpr uint32_t kIdCueTrackPositions = 0xB7;
inline constexpr uint32_t kIdCueTrack = 0xF7;
inline constexpr uint32_t kIdCueClusterPosition = 0xF1;

[[nodiscard]] inline bool IsSegmentChildId(uint32_t id) {
  return id == kIdSeekHead || id == kIdInfo || id == kIdTracks ||
         id == kIdCluster || id == kIdCues || id == kIdTags ||
         id == kIdChapters || id == kIdAttachments;
}

[[nodiscard]] inline bool IsLevelOneId(uint32_t id) {
  return IsSegmentChildId(id) || id == kIdSegment || id == kIdEbml;
}

}

#endif
