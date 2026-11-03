#include "umbra/demux/mkv_elements.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/ebml.h"

namespace umbra::demux {
namespace {

constexpr uint64_t kSupportedDocTypeReadVersion = 4;
constexpr uint64_t kCompressionHeaderStripping = 3;
constexpr uint64_t kScopeFrames = 1;
constexpr uint64_t kEncodingTypeEncryption = 1;
constexpr uint64_t kMaxChannels = 255;
constexpr double kMaxSamplingFrequency = 1000000.0;
constexpr uint64_t kMaxHeaderStripBytes = 1024;

bool IsMatroskaDocType(const std::string& doc_type) {
  return doc_type == "webm" || doc_type == "matroska";
}

absl::StatusOr<MkvColour> ParseColour(EbmlBytes payload) {
  MkvColour colour;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    ALUMBRA_ASSIGN_OR_RETURN(const uint64_t value,
                             ReadEbmlUnsigned(element.payload));
    if (element.id == kIdMatrixCoefficients) {
      colour.matrix = value;
    } else if (element.id == kIdTransferCharacteristics) {
      colour.transfer = value;
    } else if (element.id == kIdPrimaries) {
      colour.primaries = value;
    } else if (element.id == kIdRange) {
      colour.range = value;
    }
  }
  return colour;
}

absl::StatusOr<MkvVideo> ParseVideo(EbmlBytes payload) {
  MkvVideo video;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdColour) {
      ALUMBRA_ASSIGN_OR_RETURN(video.colour, ParseColour(element.payload));
      continue;
    }
    if (element.id != kIdPixelWidth && element.id != kIdPixelHeight &&
        element.id != kIdDisplayWidth && element.id != kIdDisplayHeight &&
        element.id != kIdDisplayUnit) {
      continue;
    }
    ALUMBRA_ASSIGN_OR_RETURN(const uint64_t value,
                             ReadEbmlUnsigned(element.payload));
    if (element.id == kIdPixelWidth) {
      video.pixel_width = value;
    } else if (element.id == kIdPixelHeight) {
      video.pixel_height = value;
    } else if (element.id == kIdDisplayWidth) {
      video.display_width = value;
    } else if (element.id == kIdDisplayHeight) {
      video.display_height = value;
    } else {
      video.display_unit = value;
    }
  }
  return video;
}

absl::StatusOr<MkvAudio> ParseAudio(EbmlBytes payload) {
  MkvAudio audio;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdSamplingFrequency) {
      ALUMBRA_ASSIGN_OR_RETURN(audio.sampling_frequency,
                               ReadEbmlFloat(element.payload));
      if (!std::isfinite(audio.sampling_frequency) ||
          audio.sampling_frequency <= 0 ||
          audio.sampling_frequency > kMaxSamplingFrequency) {
        return codec::Corrupt("audio sampling frequency is out of range");
      }
    } else if (element.id == kIdChannels) {
      ALUMBRA_ASSIGN_OR_RETURN(audio.channels,
                               ReadEbmlUnsigned(element.payload));
      if (audio.channels == 0 || audio.channels > kMaxChannels) {
        return codec::Corrupt("audio channel count is out of range");
      }
    }
  }
  return audio;
}

struct ContentEncodingState {
  size_t count = 0;
  uint64_t scope = kScopeFrames;
  uint64_t type = 0;
  uint64_t algorithm = 0;
  bool has_compression = false;
  std::vector<uint8_t> settings;
};

absl::Status ParseCompression(EbmlBytes payload, ContentEncodingState& state) {
  state.has_compression = true;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdContentCompAlgo) {
      ALUMBRA_ASSIGN_OR_RETURN(state.algorithm,
                               ReadEbmlUnsigned(element.payload));
    } else if (element.id == kIdContentCompSettings) {
      if (element.payload.size() > kMaxHeaderStripBytes) {
        return codec::Corrupt("header stripping bytes are too long");
      }
      state.settings.assign(element.payload.begin(), element.payload.end());
    }
  }
  return absl::OkStatus();
}

absl::Status ParseContentEncoding(EbmlBytes payload,
                                  ContentEncodingState& state) {
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdContentEncodingScope) {
      ALUMBRA_ASSIGN_OR_RETURN(state.scope, ReadEbmlUnsigned(element.payload));
    } else if (element.id == kIdContentEncodingType) {
      ALUMBRA_ASSIGN_OR_RETURN(state.type, ReadEbmlUnsigned(element.payload));
    } else if (element.id == kIdContentCompression) {
      ALUMBRA_RETURN_IF_ERROR(ParseCompression(element.payload, state));
    }
  }
  return absl::OkStatus();
}

absl::Status ParseContentEncodings(EbmlBytes payload, MkvTrack& track) {
  ContentEncodingState state;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id != kIdContentEncoding) {
      continue;
    }
    ++state.count;
    if (state.count > 1) {
      track.unsupported_encoding = true;
      continue;
    }
    ALUMBRA_RETURN_IF_ERROR(ParseContentEncoding(element.payload, state));
  }
  if (state.count == 0) {
    return absl::OkStatus();
  }
  if (state.type == kEncodingTypeEncryption) {
    track.encrypted = true;
    return absl::OkStatus();
  }
  if (!state.has_compression ||
      state.algorithm != kCompressionHeaderStripping) {
    track.unsupported_encoding = true;
    return absl::OkStatus();
  }
  if ((state.scope & kScopeFrames) != 0) {
    track.header_strip = std::move(state.settings);
  }
  return absl::OkStatus();
}

uint64_t* UintFieldOf(MkvTrack& track, uint32_t id) {
  if (id == kIdTrackNumber) {
    return &track.number;
  }
  if (id == kIdTrackType) {
    return &track.type;
  }
  if (id == kIdCodecDelay) {
    return &track.codec_delay_ns;
  }
  if (id == kIdSeekPreRoll) {
    return &track.seek_pre_roll_ns;
  }
  return nullptr;
}

absl::Status ParseTrackText(const EbmlElement& element, MkvTrack& track) {
  if (element.id == kIdCodecId) {
    ALUMBRA_ASSIGN_OR_RETURN(track.codec_id, ReadEbmlString(element.payload));
  } else if (element.id == kIdCodecPrivate) {
    track.codec_private.assign(element.payload.begin(), element.payload.end());
  } else if (element.id == kIdLanguageBcp47) {
    ALUMBRA_ASSIGN_OR_RETURN(track.language, ReadEbmlString(element.payload));
    track.has_language_override = true;
  } else if (!track.has_language_override) {
    ALUMBRA_ASSIGN_OR_RETURN(track.language, ReadEbmlString(element.payload));
  }
  return absl::OkStatus();
}

absl::Status ParseTrackMaster(const EbmlElement& element, MkvTrack& track) {
  if (element.id == kIdVideo) {
    ALUMBRA_ASSIGN_OR_RETURN(track.video, ParseVideo(element.payload));
  } else if (element.id == kIdAudio) {
    ALUMBRA_ASSIGN_OR_RETURN(track.audio, ParseAudio(element.payload));
  } else if (element.id == kIdContentEncodings) {
    ALUMBRA_RETURN_IF_ERROR(ParseContentEncodings(element.payload, track));
  }
  return absl::OkStatus();
}

absl::Status ParseTrackField(const EbmlElement& element, MkvTrack& track) {
  const uint32_t id = element.id;
  if (uint64_t* field = UintFieldOf(track, id); field != nullptr) {
    ALUMBRA_ASSIGN_OR_RETURN(*field, ReadEbmlUnsigned(element.payload));
    return absl::OkStatus();
  }
  if (id == kIdDefaultDuration) {
    ALUMBRA_ASSIGN_OR_RETURN(track.default_duration_ns,
                             ReadEbmlUnsigned(element.payload));
    return absl::OkStatus();
  }
  if (id == kIdCodecId || id == kIdCodecPrivate || id == kIdLanguage ||
      id == kIdLanguageBcp47) {
    return ParseTrackText(element, track);
  }
  return ParseTrackMaster(element, track);
}

absl::StatusOr<MkvTrack> ParseTrackEntry(EbmlBytes payload) {
  MkvTrack track;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    ALUMBRA_RETURN_IF_ERROR(ParseTrackField(element, track));
  }
  if (track.number == 0 ||
      track.number > std::numeric_limits<uint32_t>::max()) {
    return codec::Corrupt("track number is missing or out of range");
  }
  return track;
}

absl::StatusOr<bool> ParseCueTrackPositions(EbmlBytes payload,
                                            MkvCuePoint& point) {
  bool has_position = false;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement field, reader.Next());
    if (field.id == kIdCueTrack) {
      ALUMBRA_ASSIGN_OR_RETURN(point.track, ReadEbmlUnsigned(field.payload));
    } else if (field.id == kIdCueClusterPosition) {
      ALUMBRA_ASSIGN_OR_RETURN(point.cluster_position,
                               ReadEbmlUnsigned(field.payload));
      has_position = true;
    }
  }
  return has_position;
}

absl::StatusOr<std::pair<MkvCuePoint, bool>> ParseCuePoint(EbmlBytes payload) {
  MkvCuePoint point;
  bool has_position = false;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdCueTime) {
      ALUMBRA_ASSIGN_OR_RETURN(point.time, ReadEbmlUnsigned(element.payload));
    } else if (element.id == kIdCueTrackPositions && !has_position) {
      ALUMBRA_ASSIGN_OR_RETURN(has_position,
                               ParseCueTrackPositions(element.payload, point));
    }
  }
  return std::make_pair(point, has_position);
}

absl::StatusOr<std::pair<std::optional<uint64_t>, std::optional<uint64_t>>>
ParseSeekEntry(EbmlBytes payload) {
  std::optional<uint64_t> target;
  std::optional<uint64_t> position;
  EbmlReader entry(payload);
  while (!entry.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement field, entry.Next());
    if (field.id == kIdSeekId && field.payload.size() <= kMaxEbmlIdBytes) {
      ALUMBRA_ASSIGN_OR_RETURN(target, ReadEbmlUnsigned(field.payload));
    } else if (field.id == kIdSeekPosition) {
      ALUMBRA_ASSIGN_OR_RETURN(position, ReadEbmlUnsigned(field.payload));
    }
  }
  return std::make_pair(target, position);
}

}

absl::Status ParseEbmlHeaderElement(EbmlBytes payload) {
  uint64_t read_version = 1;
  uint64_t max_id_length = 4;
  uint64_t max_size_length = 8;
  uint64_t doc_type_read_version = 1;
  std::optional<std::string> doc_type;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdDocType) {
      ALUMBRA_ASSIGN_OR_RETURN(doc_type, ReadEbmlString(element.payload));
      continue;
    }
    if (element.id != kIdEbmlReadVersion && element.id != kIdEbmlMaxIdLength &&
        element.id != kIdEbmlMaxSizeLength &&
        element.id != kIdDocTypeReadVersion) {
      continue;
    }
    ALUMBRA_ASSIGN_OR_RETURN(const uint64_t value,
                             ReadEbmlUnsigned(element.payload));
    if (element.id == kIdEbmlReadVersion) {
      read_version = value;
    } else if (element.id == kIdEbmlMaxIdLength) {
      max_id_length = value;
    } else if (element.id == kIdEbmlMaxSizeLength) {
      max_size_length = value;
    } else {
      doc_type_read_version = value;
    }
  }
  if (!doc_type.has_value()) {
    return codec::Corrupt("EBML header has no DocType");
  }
  if (!IsMatroskaDocType(*doc_type)) {
    return codec::Unsupported(absl::StrCat("EBML DocType ", *doc_type));
  }
  if (read_version != 1 || max_id_length > kMaxEbmlIdBytes ||
      max_size_length > kMaxEbmlSizeBytes ||
      doc_type_read_version > kSupportedDocTypeReadVersion) {
    return codec::Unsupported("EBML header requires a newer reader");
  }
  return absl::OkStatus();
}

absl::StatusOr<MkvSeekHead> ParseSeekHead(EbmlBytes payload) {
  MkvSeekHead head;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id != kIdSeek) {
      continue;
    }
    ALUMBRA_ASSIGN_OR_RETURN(const auto entry, ParseSeekEntry(element.payload));
    if (entry.first == kIdCues && entry.second.has_value() &&
        !head.cues_position.has_value()) {
      head.cues_position = entry.second;
    }
  }
  return head;
}

absl::StatusOr<MkvInfo> ParseInfo(EbmlBytes payload) {
  MkvInfo info;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdTimestampScale) {
      ALUMBRA_ASSIGN_OR_RETURN(info.timestamp_scale,
                               ReadEbmlUnsigned(element.payload));
      if (info.timestamp_scale == 0 ||
          info.timestamp_scale >
              static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return codec::Corrupt("timestamp scale is out of range");
      }
    } else if (element.id == kIdDuration) {
      ALUMBRA_ASSIGN_OR_RETURN(const double duration,
                               ReadEbmlFloat(element.payload));
      if (std::isfinite(duration) && duration > 0 &&
          duration < static_cast<double>(kMaxMkvTimeline)) {
        info.duration_ticks = duration;
      }
    }
  }
  return info;
}

absl::StatusOr<std::vector<MkvTrack>> ParseTracks(EbmlBytes payload) {
  std::vector<MkvTrack> tracks;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id != kIdTrackEntry) {
      continue;
    }
    if (tracks.size() >= kMaxMkvTracks) {
      return codec::Corrupt("file has too many tracks");
    }
    ALUMBRA_ASSIGN_OR_RETURN(MkvTrack track, ParseTrackEntry(element.payload));
    const bool duplicate = std::ranges::any_of(
        tracks,
        [&](const MkvTrack& other) { return other.number == track.number; });
    if (duplicate) {
      return codec::Corrupt("two tracks share a track number");
    }
    tracks.push_back(std::move(track));
  }
  return tracks;
}

absl::StatusOr<std::vector<MkvCuePoint>> ParseCues(EbmlBytes payload) {
  std::vector<MkvCuePoint> points;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id != kIdCuePoint) {
      continue;
    }
    if (points.size() >= kMaxCuePoints) {
      return codec::Corrupt("file has too many cue points");
    }
    ALUMBRA_ASSIGN_OR_RETURN(const auto parsed, ParseCuePoint(element.payload));
    if (parsed.second) {
      points.push_back(parsed.first);
    }
  }
  return points;
}

absl::StatusOr<MkvBlockGroup> ParseBlockGroup(EbmlBytes payload) {
  MkvBlockGroup group;
  bool has_block = false;
  EbmlReader reader(payload);
  while (!reader.AtEnd()) {
    ALUMBRA_ASSIGN_OR_RETURN(const EbmlElement element, reader.Next());
    if (element.id == kIdBlock && !has_block) {
      has_block = true;
      group.block_offset =
          static_cast<size_t>(element.payload.data() - payload.data());
      group.block_size = element.payload.size();
    } else if (element.id == kIdBlockDuration) {
      uint64_t duration = 0;
      ALUMBRA_ASSIGN_OR_RETURN(duration, ReadEbmlUnsigned(element.payload));
      group.duration = duration;
    } else if (element.id == kIdReferenceBlock) {
      group.has_reference = true;
    } else if (element.id == kIdDiscardPadding) {
      ALUMBRA_ASSIGN_OR_RETURN(const int64_t padding,
                               ReadEbmlSigned(element.payload));
      group.discard_padding_ns = padding;
    }
  }
  if (!has_block) {
    return codec::Corrupt("block group has no block");
  }
  return group;
}

}
