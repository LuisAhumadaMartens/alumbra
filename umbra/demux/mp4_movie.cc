#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "penumbra/interface/rational.h"
#include "umbra/base/status.h"
#include "umbra/codec/reader_util.h"
#include "umbra/demux/mp4_box.h"
#include "umbra/demux/mp4_sample_entry.h"
#include "umbra/demux/mp4_tables.h"
#include "umbra/demux/mp4_types.h"

namespace umbra::demux {
namespace {

constexpr uint32_t kMvhd = FourCc('m', 'v', 'h', 'd');
constexpr uint32_t kMehd = FourCc('m', 'e', 'h', 'd');
constexpr uint32_t kTrex = FourCc('t', 'r', 'e', 'x');
constexpr uint32_t kTrak = FourCc('t', 'r', 'a', 'k');
constexpr uint32_t kTkhd = FourCc('t', 'k', 'h', 'd');
constexpr uint32_t kElst = FourCc('e', 'l', 's', 't');
constexpr uint32_t kMdhd = FourCc('m', 'd', 'h', 'd');
constexpr uint32_t kHdlr = FourCc('h', 'd', 'l', 'r');
constexpr uint32_t kStsd = FourCc('s', 't', 's', 'd');
constexpr uint32_t kStts = FourCc('s', 't', 't', 's');
constexpr uint32_t kCtts = FourCc('c', 't', 't', 's');
constexpr uint32_t kStsc = FourCc('s', 't', 's', 'c');
constexpr uint32_t kStsz = FourCc('s', 't', 's', 'z');
constexpr uint32_t kStz2 = FourCc('s', 't', 'z', '2');
constexpr uint32_t kStco = FourCc('s', 't', 'c', 'o');
constexpr uint32_t kCo64 = FourCc('c', 'o', '6', '4');
constexpr uint32_t kStss = FourCc('s', 't', 's', 's');
constexpr uint32_t kMdia = FourCc('m', 'd', 'i', 'a');
constexpr uint32_t kMinf = FourCc('m', 'i', 'n', 'f');
constexpr uint32_t kStbl = FourCc('s', 't', 'b', 'l');
constexpr uint32_t kEdts = FourCc('e', 'd', 't', 's');
constexpr uint32_t kMvex = FourCc('m', 'v', 'e', 'x');

constexpr size_t kNoTrack = static_cast<size_t>(-1);

struct TrexEntry {
  uint32_t track_id = 0;
  TrackDefaults defaults;
};

absl::StatusOr<uint8_t> ReadFullBox(codec::RecordReader& reader) {
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t version_and_flags, reader.U32());
  return static_cast<uint8_t>(version_and_flags >> 24);
}

absl::StatusOr<uint32_t> ReadCount(codec::RecordReader& reader,
                                   size_t entry_bytes) {
  ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, reader.U32());
  if (static_cast<uint64_t>(count) * entry_bytes > reader.remaining()) {
    return codec::Corrupt(
        absl::StrCat("table of ", count, " entries exceeds its box"));
  }
  return count;
}

std::string DecodeLanguage(uint16_t packed) {
  std::string language(3, ' ');
  for (size_t index = 0; index < 3; ++index) {
    const int shift = 10 - (static_cast<int>(index) * 5);
    language[index] = static_cast<char>(((packed >> shift) & 0x1F) + 0x60);
  }
  return language;
}

class MovieParser {
 public:
  absl::StatusOr<Movie> Parse(std::span<const uint8_t> moov) {
    ALUMBRA_RETURN_IF_ERROR(Walk(moov, 1));
    for (const TrexEntry& trex : trex_) {
      for (TrackDef& track : movie_.tracks) {
        if (track.track_id == trex.track_id) {
          track.defaults = trex.defaults;
        }
      }
    }
    return std::move(movie_);
  }

 private:
  absl::Status Walk(std::span<const uint8_t> payload, size_t depth) {
    ALUMBRA_ASSIGN_OR_RETURN(const std::vector<ChildBox> children,
                             SplitChildren(payload, depth));
    for (const ChildBox& child : children) {
      ALUMBRA_RETURN_IF_ERROR(Dispatch(child, depth));
    }
    return absl::OkStatus();
  }

  absl::Status Dispatch(const ChildBox& box, size_t depth) {
    if (box.type == kTrak) {
      return EnterTrack(box.payload, depth);
    }
    if (box.type == kMdia || box.type == kMinf || box.type == kStbl ||
        box.type == kEdts) {
      return Walk(box.payload, depth + 1);
    }
    if (box.type == kMvex) {
      movie_.has_mvex = true;
      return Walk(box.payload, depth + 1);
    }
    if (box.type == kMvhd) {
      return ParseMvhd(box.payload);
    }
    if (box.type == kMehd) {
      return ParseMehd(box.payload);
    }
    if (box.type == kTrex) {
      return ParseTrex(box.payload);
    }
    if (current_ == kNoTrack) {
      return absl::OkStatus();
    }
    return DispatchTrackBox(box);
  }

  absl::Status DispatchTrackBox(const ChildBox& box) {
    if (box.type == kTkhd) {
      return ParseTkhd(box.payload);
    }
    if (box.type == kElst) {
      return ParseElst(box.payload);
    }
    if (box.type == kMdhd) {
      return ParseMdhd(box.payload);
    }
    if (box.type == kHdlr) {
      return ParseHdlr(box.payload);
    }
    if (box.type == kStsd) {
      return ParseStsd(box.payload);
    }
    return DispatchTableBox(box);
  }

  absl::Status DispatchTableBox(const ChildBox& box) {
    if (box.type == kStts) {
      return ParseStts(box.payload);
    }
    if (box.type == kCtts) {
      return ParseCtts(box.payload);
    }
    if (box.type == kStsc) {
      return ParseStsc(box.payload);
    }
    if (box.type == kStsz) {
      return ParseStsz(box.payload);
    }
    if (box.type == kStz2) {
      return ParseStz2(box.payload);
    }
    if (box.type == kStco) {
      return ParseChunkOffsets(box.payload, 4);
    }
    if (box.type == kCo64) {
      return ParseChunkOffsets(box.payload, 8);
    }
    if (box.type == kStss) {
      return ParseStss(box.payload);
    }
    return absl::OkStatus();
  }

  TrackDef& Track() { return movie_.tracks[current_]; }

  absl::Status EnterTrack(std::span<const uint8_t> payload, size_t depth) {
    if (movie_.tracks.size() >= kMaxTracks) {
      return codec::Corrupt("movie has too many tracks");
    }
    const size_t saved = current_;
    movie_.tracks.emplace_back();
    current_ = movie_.tracks.size() - 1;
    const absl::Status status = Walk(payload, depth + 1);
    current_ = saved;
    return status;
  }

  absl::Status ParseMvhd(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "mvhd");
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t version, ReadFullBox(reader));
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(version == 1 ? 16 : 8));
    ALUMBRA_ASSIGN_OR_RETURN(movie_.movie_timescale, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(movie_.movie_duration,
                             reader.Unsigned(version == 1 ? 8 : 4));
    return absl::OkStatus();
  }

  absl::Status ParseMehd(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "mehd");
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t version, ReadFullBox(reader));
    ALUMBRA_ASSIGN_OR_RETURN(movie_.fragment_duration,
                             reader.Unsigned(version == 1 ? 8 : 4));
    return absl::OkStatus();
  }

  absl::Status ParseTrex(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "trex");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    TrexEntry entry;
    ALUMBRA_ASSIGN_OR_RETURN(entry.track_id, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(entry.defaults.description_index, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(entry.defaults.duration, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(entry.defaults.size, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(entry.defaults.flags, reader.U32());
    trex_.push_back(entry);
    return absl::OkStatus();
  }

  absl::Status ParseTkhd(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "tkhd");
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t version, ReadFullBox(reader));
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(version == 1 ? 16 : 8));
    ALUMBRA_ASSIGN_OR_RETURN(Track().track_id, reader.U32());
    return absl::OkStatus();
  }

  absl::Status ParseElst(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "elst");
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t version, ReadFullBox(reader));
    const size_t entry_bytes = version == 1 ? 20 : 12;
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count,
                             ReadCount(reader, entry_bytes));
    Track().edits.clear();
    for (uint32_t index = 0; index < count; ++index) {
      EditEntry edit;
      ALUMBRA_ASSIGN_OR_RETURN(edit.segment_duration,
                               reader.Unsigned(version == 1 ? 8 : 4));
      if (version == 1) {
        ALUMBRA_ASSIGN_OR_RETURN(const uint64_t time, reader.Unsigned(8));
        edit.media_time = static_cast<int64_t>(time);
      } else {
        ALUMBRA_ASSIGN_OR_RETURN(const uint32_t time, reader.U32());
        edit.media_time = static_cast<int32_t>(time);
      }
      ALUMBRA_RETURN_IF_ERROR(reader.Skip(4));
      Track().edits.push_back(edit);
    }
    return absl::OkStatus();
  }

  absl::Status ParseMdhd(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "mdhd");
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t version, ReadFullBox(reader));
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(version == 1 ? 16 : 8));
    ALUMBRA_ASSIGN_OR_RETURN(Track().timescale, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(Track().duration,
                             reader.Unsigned(version == 1 ? 8 : 4));
    ALUMBRA_ASSIGN_OR_RETURN(const uint16_t packed, reader.U16());
    Track().language = DecodeLanguage(packed);
    return absl::OkStatus();
  }

  absl::Status ParseHdlr(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "hdlr");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(4));
    ALUMBRA_ASSIGN_OR_RETURN(Track().handler, reader.U32());
    return absl::OkStatus();
  }

  absl::Status ParseStsd(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stsd");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, reader.U32());
    if (count > kMaxSampleEntries) {
      return codec::Corrupt("stsd has too many sample entries");
    }
    ALUMBRA_ASSIGN_OR_RETURN(const std::span<const uint8_t> rest,
                             reader.Take(reader.remaining()));
    ALUMBRA_ASSIGN_OR_RETURN(const std::vector<ChildBox> entries,
                             SplitChildren(rest, 5));
    SampleEntryContext context;
    context.track_id = Track().track_id;
    context.language = Track().language;
    context.handler = Track().handler;
    const std::optional<penumbra::Rational> time_base =
        penumbra::Rational::Create(1, Track().timescale);
    context.time_base = time_base.value_or(penumbra::Rational());
    Track().entries.clear();
    for (const ChildBox& entry : entries) {
      absl::StatusOr<SampleEntryInfo> parsed =
          ParseSampleEntry(entry.type, entry.payload, context);
      if (!parsed.ok() &&
          parsed.status().code() != absl::StatusCode::kUnimplemented) {
        return parsed.status();
      }
      Track().entries.push_back(parsed.ok() ? std::move(*parsed)
                                            : SampleEntryInfo{});
    }
    return absl::OkStatus();
  }

  absl::Status ParseStts(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stts");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, ReadCount(reader, 8));
    Track().table.stts.resize(count);
    for (SttsEntry& entry : Track().table.stts) {
      ALUMBRA_ASSIGN_OR_RETURN(entry.count, reader.U32());
      ALUMBRA_ASSIGN_OR_RETURN(entry.delta, reader.U32());
    }
    return absl::OkStatus();
  }

  absl::Status ParseCtts(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "ctts");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, ReadCount(reader, 8));
    Track().table.ctts.resize(count);
    for (CttsEntry& entry : Track().table.ctts) {
      ALUMBRA_ASSIGN_OR_RETURN(entry.count, reader.U32());
      ALUMBRA_ASSIGN_OR_RETURN(const uint32_t offset, reader.U32());
      entry.offset = static_cast<int32_t>(offset);
    }
    return absl::OkStatus();
  }

  absl::Status ParseStsc(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stsc");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, ReadCount(reader, 12));
    Track().table.stsc.resize(count);
    for (StscEntry& entry : Track().table.stsc) {
      ALUMBRA_ASSIGN_OR_RETURN(entry.first_chunk, reader.U32());
      ALUMBRA_ASSIGN_OR_RETURN(entry.samples_per_chunk, reader.U32());
      ALUMBRA_ASSIGN_OR_RETURN(entry.description_index, reader.U32());
    }
    return absl::OkStatus();
  }

  absl::Status ParseStsz(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stsz");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    RawSampleTable& table = Track().table;
    ALUMBRA_ASSIGN_OR_RETURN(table.constant_size, reader.U32());
    ALUMBRA_ASSIGN_OR_RETURN(table.sample_count, reader.U32());
    table.has_sizes = true;
    table.sizes.clear();
    if (table.constant_size != 0) {
      return absl::OkStatus();
    }
    if (static_cast<uint64_t>(table.sample_count) * 4 > reader.remaining()) {
      return codec::Corrupt("stsz sizes exceed their box");
    }
    table.sizes.resize(table.sample_count);
    for (uint32_t& size : table.sizes) {
      ALUMBRA_ASSIGN_OR_RETURN(size, reader.U32());
    }
    return absl::OkStatus();
  }

  absl::Status ParseStz2(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stz2");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_RETURN_IF_ERROR(reader.Skip(3));
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t field_size, reader.U8());
    RawSampleTable& table = Track().table;
    ALUMBRA_ASSIGN_OR_RETURN(table.sample_count, reader.U32());
    if (field_size != 4 && field_size != 8 && field_size != 16) {
      return codec::Corrupt("stz2 field size is invalid");
    }
    if (static_cast<uint64_t>(table.sample_count) * field_size >
        static_cast<uint64_t>(reader.remaining()) * 8) {
      return codec::Corrupt("stz2 sizes exceed their box");
    }
    table.has_sizes = true;
    table.constant_size = 0;
    table.sizes.assign(table.sample_count, 0);
    return ReadPackedSizes(reader, field_size, table.sizes);
  }

  static absl::Status ReadPackedSizes(codec::RecordReader& reader,
                                      uint8_t field_size,
                                      std::vector<uint32_t>& sizes) {
    uint8_t pending = 0;
    for (size_t index = 0; index < sizes.size(); ++index) {
      if (field_size == 4) {
        if (index % 2 == 0) {
          ALUMBRA_ASSIGN_OR_RETURN(pending, reader.U8());
          sizes[index] = pending >> 4;
        } else {
          sizes[index] = pending & 0x0F;
        }
      } else if (field_size == 8) {
        ALUMBRA_ASSIGN_OR_RETURN(sizes[index], ReadU8AsU32(reader));
      } else {
        ALUMBRA_ASSIGN_OR_RETURN(sizes[index], ReadU16AsU32(reader));
      }
    }
    return absl::OkStatus();
  }

  static absl::StatusOr<uint32_t> ReadU8AsU32(codec::RecordReader& reader) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint8_t value, reader.U8());
    return static_cast<uint32_t>(value);
  }

  static absl::StatusOr<uint32_t> ReadU16AsU32(codec::RecordReader& reader) {
    ALUMBRA_ASSIGN_OR_RETURN(const uint16_t value, reader.U16());
    return static_cast<uint32_t>(value);
  }

  absl::Status ParseChunkOffsets(std::span<const uint8_t> payload,
                                 size_t width) {
    codec::RecordReader reader(payload, "chunk offsets");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, ReadCount(reader, width));
    Track().table.chunk_offsets.resize(count);
    for (uint64_t& offset : Track().table.chunk_offsets) {
      ALUMBRA_ASSIGN_OR_RETURN(offset, reader.Unsigned(width));
    }
    return absl::OkStatus();
  }

  absl::Status ParseStss(std::span<const uint8_t> payload) {
    codec::RecordReader reader(payload, "stss");
    ALUMBRA_RETURN_IF_ERROR(ReadFullBox(reader).status());
    ALUMBRA_ASSIGN_OR_RETURN(const uint32_t count, ReadCount(reader, 4));
    RawSampleTable& table = Track().table;
    table.has_sync_table = true;
    table.sync_samples.resize(count);
    for (uint32_t& number : table.sync_samples) {
      ALUMBRA_ASSIGN_OR_RETURN(number, reader.U32());
    }
    return absl::OkStatus();
  }

  Movie movie_;
  std::vector<TrexEntry> trex_;
  size_t current_ = kNoTrack;
};

}

absl::StatusOr<Movie> ParseMovie(std::span<const uint8_t> moov_payload) {
  MovieParser parser;
  return parser.Parse(moov_payload);
}

}
