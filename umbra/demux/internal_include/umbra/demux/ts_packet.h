#ifndef UMBRA_DEMUX_TS_PACKET_H_
#define UMBRA_DEMUX_TS_PACKET_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace umbra::demux::ts {

inline constexpr uint8_t kSyncByte = 0x47;
inline constexpr size_t kPacketSize = 188;
inline constexpr uint16_t kPatPid = 0;
inline constexpr uint16_t kNullPid = 0x1FFF;
inline constexpr size_t kMaxSectionBytes = 4096;
inline constexpr uint64_t kTimestampWrap = uint64_t{1} << 33;

struct AdaptationField {
  bool discontinuity = false;
  bool random_access = false;
  std::optional<uint64_t> pcr_27mhz;
};

struct PacketHeader {
  bool transport_error = false;
  bool payload_unit_start = false;
  uint16_t pid = 0;
  uint8_t scrambling = 0;
  bool has_payload = false;
  uint8_t continuity = 0;
  AdaptationField adaptation;
  std::span<const uint8_t> payload;
};

[[nodiscard]] std::optional<PacketHeader> ParsePacket(
    std::span<const uint8_t> packet);

[[nodiscard]] uint32_t Crc32Mpeg(std::span<const uint8_t> data);

struct Section {
  std::vector<uint8_t> bytes;
  bool crc_ok = false;
};

class SectionAssembler {
 public:
  void Reset();
  void Feed(std::span<const uint8_t> payload, bool payload_unit_start,
            std::vector<Section>& complete);

  [[nodiscard]] size_t OversizedSections() const { return oversized_; }

 private:
  void Consume(std::span<const uint8_t> data, std::vector<Section>& complete);

  std::vector<uint8_t> buffer_;
  size_t expected_ = 0;
  bool active_ = false;
  size_t oversized_ = 0;
};

struct PatEntry {
  uint16_t program_number = 0;
  uint16_t pid = 0;
};

struct Pat {
  uint8_t version = 0;
  std::vector<PatEntry> programs;
};

[[nodiscard]] std::optional<Pat> ParsePat(std::span<const uint8_t> section);

struct EsDescriptors {
  bool ac3 = false;
  bool eac3 = false;
  bool id3 = false;
  std::string language;

  friend bool operator==(const EsDescriptors&, const EsDescriptors&) = default;
};

struct PmtStream {
  uint8_t stream_type = 0;
  uint16_t pid = 0;
  EsDescriptors descriptors;

  friend bool operator==(const PmtStream&, const PmtStream&) = default;
};

struct Pmt {
  uint16_t program_number = 0;
  uint8_t version = 0;
  uint16_t pcr_pid = 0;
  std::vector<PmtStream> streams;
};

[[nodiscard]] std::optional<Pmt> ParsePmt(std::span<const uint8_t> section);

struct PesHeader {
  uint8_t stream_id = 0;
  uint16_t packet_length = 0;
  std::optional<uint64_t> pts;
  std::optional<uint64_t> dts;
  size_t payload_offset = 0;
};

[[nodiscard]] bool StreamIdHasPesHeader(uint8_t stream_id);

[[nodiscard]] std::optional<PesHeader> ParsePesHeader(
    std::span<const uint8_t> pes);

class TimestampUnwrapper {
 public:
  [[nodiscard]] int64_t Unwrap(uint64_t raw);
  void Seed(int64_t reference) { reference_ = reference; }
  void Reset() { reference_.reset(); }

 private:
  std::optional<int64_t> reference_;
};

}

#endif
