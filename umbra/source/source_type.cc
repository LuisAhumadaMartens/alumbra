#include "umbra/source/source_type.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace umbra::source {
namespace {

constexpr size_t kTsPacketBytes = 188;
constexpr size_t kTsSyncChecks = 3;
constexpr size_t kMp4HeaderBytes = 8;
constexpr size_t kTextProbeBytes = 512;
constexpr uint8_t kTsSync = 0x47;
constexpr std::string_view kHlsSignature = "#EXTM3U";
constexpr std::array<uint8_t, 3> kUtf8Bom = {0xEF, 0xBB, 0xBF};
constexpr std::array<uint8_t, 4> kEbmlMagic = {0x1A, 0x45, 0xDF, 0xA3};
constexpr std::array<std::string_view, 8> kMp4Boxes = {
    "ftyp", "styp", "moov", "moof", "mdat", "free", "skip", "wide"};

enum class Match { kType, kNeedMore, kNone, kRejected };

struct SignatureResult {
  Match match = Match::kNone;
  SourceType type = SourceType::kMp4;
  size_t wanted = 0;
};

SignatureResult Found(SourceType type) {
  return {.match = Match::kType, .type = type};
}

SignatureResult NeedMore(size_t wanted) {
  return {.match = Match::kNeedMore, .wanted = wanted};
}

SignatureResult NeedMoreTextBudget(size_t size) {
  return NeedMore(std::min(kMaxSniffBytes, std::max(kSniffBytes, size * 2)));
}

std::string_view AsText(std::span<const uint8_t> bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

bool IsSpace(uint8_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string Lower(std::string_view text) {
  std::string result(text);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return result;
}

bool IsMp4Header(std::span<const uint8_t> bytes) {
  if (bytes.size() < kMp4HeaderBytes) {
    return false;
  }
  const uint32_t box_size = (static_cast<uint32_t>(bytes[0]) << 24) |
                            (static_cast<uint32_t>(bytes[1]) << 16) |
                            (static_cast<uint32_t>(bytes[2]) << 8) |
                            static_cast<uint32_t>(bytes[3]);
  if (box_size != 0 && box_size != 1 && box_size < kMp4HeaderBytes) {
    return false;
  }
  const std::string_view type = AsText(bytes).substr(4, 4);
  return std::ranges::find(kMp4Boxes, type) != kMp4Boxes.end();
}

SignatureResult MatchTransportStream(std::span<const uint8_t> bytes,
                                     bool end_of_source) {
  const size_t needed = ((kTsSyncChecks - 1) * kTsPacketBytes) + 1;
  if (bytes.size() < needed && !end_of_source) {
    return NeedMore(needed);
  }
  if (bytes.size() < kTsPacketBytes) {
    return {};
  }
  for (size_t i = 0; i < kTsSyncChecks; ++i) {
    const size_t position = i * kTsPacketBytes;
    if (position < bytes.size() && bytes[position] != kTsSync) {
      return {};
    }
  }
  return Found(SourceType::kMpegTs);
}

SignatureResult MatchBinary(std::span<const uint8_t> bytes,
                            bool end_of_source) {
  if (bytes.size() >= kEbmlMagic.size() &&
      std::equal(kEbmlMagic.begin(), kEbmlMagic.end(), bytes.begin())) {
    return Found(SourceType::kMatroska);
  }
  if (bytes.size() >= 4 && bytes[0] == 'F' && bytes[1] == 'L' &&
      bytes[2] == 'V' && bytes[3] == 1) {
    return Found(SourceType::kFlv);
  }
  if (IsMp4Header(bytes)) {
    return Found(SourceType::kMp4);
  }
  if (bytes.front() == kTsSync) {
    return MatchTransportStream(bytes, end_of_source);
  }
  return {};
}

enum class XmlScan { kRoot, kRanOut, kNotXml };
enum class Skip { kNotAConstruct, kRanOut, kSkipped };

Skip SkipUntil(std::string_view rest, std::string_view terminator, size_t from,
               size_t& length) {
  const size_t end = rest.find(terminator, from);
  if (end == std::string_view::npos) {
    return Skip::kRanOut;
  }
  length = end + terminator.size();
  return Skip::kSkipped;
}

Skip SkipDeclaration(std::string_view rest, size_t& length) {
  int depth = 0;
  for (size_t i = 2; i < rest.size(); ++i) {
    if (rest[i] == '[') {
      ++depth;
    } else if (rest[i] == ']') {
      --depth;
    } else if (rest[i] == '>' && depth <= 0) {
      length = i + 1;
      return Skip::kSkipped;
    }
  }
  return Skip::kRanOut;
}

Skip SkipPrologueConstruct(std::string_view rest, size_t& length) {
  if (rest.starts_with("<?")) {
    return SkipUntil(rest, "?>", 2, length);
  }
  if (rest.starts_with("<!--")) {
    return SkipUntil(rest, "-->", 4, length);
  }
  if (rest.starts_with("<!")) {
    return SkipDeclaration(rest, length);
  }
  return Skip::kNotAConstruct;
}

XmlScan ReadRootName(std::string_view rest, std::string_view& root) {
  size_t length = 1;
  while (length < rest.size() && !IsSpace(static_cast<uint8_t>(rest[length])) &&
         rest[length] != '>' && rest[length] != '/') {
    ++length;
  }
  if (length >= rest.size()) {
    return XmlScan::kRanOut;
  }
  std::string_view name = rest.substr(1, length - 1);
  if (name.empty()) {
    return XmlScan::kNotXml;
  }
  const size_t colon = name.rfind(':');
  if (colon != std::string_view::npos) {
    name.remove_prefix(colon + 1);
  }
  root = name;
  return XmlScan::kRoot;
}

XmlScan ScanXmlRoot(std::string_view text, size_t position,
                    std::string_view& root) {
  while (true) {
    while (position < text.size() &&
           IsSpace(static_cast<uint8_t>(text[position]))) {
      ++position;
    }
    if (position >= text.size()) {
      return XmlScan::kRanOut;
    }
    if (text[position] != '<') {
      return XmlScan::kNotXml;
    }
    const std::string_view rest = text.substr(position);
    size_t length = 0;
    const Skip skipped = SkipPrologueConstruct(rest, length);
    if (skipped == Skip::kRanOut) {
      return XmlScan::kRanOut;
    }
    if (skipped == Skip::kNotAConstruct) {
      return ReadRootName(rest, root);
    }
    position += length;
  }
}

SignatureResult MatchText(std::span<const uint8_t> bytes, bool end_of_source) {
  size_t position = 0;
  if (bytes.size() >= kUtf8Bom.size() &&
      std::equal(kUtf8Bom.begin(), kUtf8Bom.end(), bytes.begin())) {
    position = kUtf8Bom.size();
  } else if (!end_of_source && bytes.size() < kUtf8Bom.size() &&
             std::equal(bytes.begin(), bytes.end(), kUtf8Bom.begin())) {
    return NeedMore(kSniffBytes);
  }
  const std::string_view text = AsText(bytes);
  while (position < text.size() &&
         IsSpace(static_cast<uint8_t>(text[position]))) {
    ++position;
  }
  if (position >= text.size()) {
    if (end_of_source || bytes.size() >= kMaxSniffBytes) {
      return {};
    }
    return NeedMoreTextBudget(bytes.size());
  }
  const std::string_view rest = text.substr(position);
  if (rest.front() == '#') {
    if (rest.starts_with(kHlsSignature)) {
      return Found(SourceType::kHls);
    }
    if (!end_of_source && rest.size() < kHlsSignature.size() &&
        kHlsSignature.starts_with(rest)) {
      return NeedMore(kSniffBytes);
    }
    return {};
  }
  if (rest.front() != '<') {
    return {};
  }
  std::string_view root;
  switch (ScanXmlRoot(text, position, root)) {
    case XmlScan::kRoot:
      return root == "MPD" ? Found(SourceType::kDash)
                           : SignatureResult{.match = Match::kRejected};
    case XmlScan::kRanOut:
      if (end_of_source || bytes.size() >= kMaxSniffBytes) {
        return {};
      }
      return NeedMoreTextBudget(bytes.size());
    case XmlScan::kNotXml:
      return {};
  }
  return {};
}

bool IsTextual(std::span<const uint8_t> bytes) {
  const size_t probe = std::min(bytes.size(), kTextProbeBytes);
  return std::ranges::none_of(bytes.first(probe), [](uint8_t c) {
    return c < 0x09 || (c > 0x0D && c < 0x20);
  });
}

bool IsTextType(SourceType type) {
  return type == SourceType::kHls || type == SourceType::kDash;
}

std::optional<SourceType> TypeFromExtension(std::string_view extension) {
  static constexpr std::array<std::pair<std::string_view, SourceType>, 16>
      kTable = {{{"m3u8", SourceType::kHls},
                 {"m3u", SourceType::kHls},
                 {"mpd", SourceType::kDash},
                 {"mp4", SourceType::kMp4},
                 {"m4v", SourceType::kMp4},
                 {"m4a", SourceType::kMp4},
                 {"m4s", SourceType::kMp4},
                 {"mov", SourceType::kMp4},
                 {"cmfv", SourceType::kMp4},
                 {"cmfa", SourceType::kMp4},
                 {"mkv", SourceType::kMatroska},
                 {"mka", SourceType::kMatroska},
                 {"webm", SourceType::kMatroska},
                 {"ts", SourceType::kMpegTs},
                 {"m2ts", SourceType::kMpegTs},
                 {"flv", SourceType::kFlv}}};
  const std::string lowered = Lower(extension);
  for (const auto& [name, type] : kTable) {
    if (name == lowered) {
      return type;
    }
  }
  return std::nullopt;
}

std::optional<SourceType> TypeFromContentType(std::string_view content_type) {
  static constexpr std::array<std::pair<std::string_view, SourceType>, 15>
      kTable = {{{"application/vnd.apple.mpegurl", SourceType::kHls},
                 {"application/x-mpegurl", SourceType::kHls},
                 {"audio/mpegurl", SourceType::kHls},
                 {"audio/x-mpegurl", SourceType::kHls},
                 {"application/dash+xml", SourceType::kDash},
                 {"video/mp4", SourceType::kMp4},
                 {"audio/mp4", SourceType::kMp4},
                 {"application/mp4", SourceType::kMp4},
                 {"video/webm", SourceType::kMatroska},
                 {"audio/webm", SourceType::kMatroska},
                 {"video/x-matroska", SourceType::kMatroska},
                 {"audio/x-matroska", SourceType::kMatroska},
                 {"video/mp2t", SourceType::kMpegTs},
                 {"video/mpeg", SourceType::kMpegTs},
                 {"video/x-flv", SourceType::kFlv}}};
  const size_t parameters = content_type.find(';');
  std::string_view essence = content_type.substr(0, parameters);
  while (!essence.empty() && IsSpace(static_cast<uint8_t>(essence.front()))) {
    essence.remove_prefix(1);
  }
  while (!essence.empty() && IsSpace(static_cast<uint8_t>(essence.back()))) {
    essence.remove_suffix(1);
  }
  const std::string lowered = Lower(essence);
  for (const auto& [name, type] : kTable) {
    if (name == lowered) {
      return type;
    }
  }
  return std::nullopt;
}

}

std::string_view SourceTypeName(SourceType type) {
  switch (type) {
    case SourceType::kHls:
      return "hls";
    case SourceType::kDash:
      return "dash";
    case SourceType::kMp4:
      return "mp4";
    case SourceType::kMatroska:
      return "matroska";
    case SourceType::kMpegTs:
      return "mpegts";
    case SourceType::kFlv:
      return "flv";
  }
  return "unknown";
}

std::string_view TypeSignalName(TypeSignal signal) {
  switch (signal) {
    case TypeSignal::kExplicitType:
      return "explicit_type";
    case TypeSignal::kSignature:
      return "signature";
    case TypeSignal::kExtension:
      return "extension";
    case TypeSignal::kContentType:
      return "content_type";
  }
  return "unknown";
}

std::optional<SourceType> ParseSourceType(std::string_view name) {
  for (const SourceType type :
       {SourceType::kHls, SourceType::kDash, SourceType::kMp4,
        SourceType::kMatroska, SourceType::kMpegTs, SourceType::kFlv}) {
    if (SourceTypeName(type) == name) {
      return type;
    }
  }
  return std::nullopt;
}

bool IsAdaptive(SourceType type) {
  return type == SourceType::kHls || type == SourceType::kDash;
}

SniffResult SniffSourceType(std::span<const uint8_t> bytes, bool end_of_source,
                            const SniffHints& hints) {
  SignatureResult signature;
  if (bytes.empty()) {
    if (!end_of_source) {
      signature = NeedMore(kSniffBytes);
    }
  } else if (bytes.size() < kMp4HeaderBytes && !end_of_source) {
    signature = NeedMore(kSniffBytes);
  } else {
    signature = MatchBinary(bytes, end_of_source);
    if (signature.match == Match::kNone) {
      signature = MatchText(bytes, end_of_source);
    }
  }
  switch (signature.match) {
    case Match::kType:
      return {.outcome = SniffOutcome::kDecided,
              .type = signature.type,
              .signal = TypeSignal::kSignature};
    case Match::kNeedMore:
      return {.outcome = SniffOutcome::kNeedMoreBytes,
              .wanted_bytes = signature.wanted};
    case Match::kRejected:
      return {};
    case Match::kNone:
      break;
  }
  if (bytes.empty()) {
    return {};
  }
  const bool textual = IsTextual(bytes);
  const auto compatible = [textual](std::optional<SourceType> type) {
    return type.has_value() && IsTextType(*type) == textual;
  };
  if (const auto type = TypeFromExtension(hints.extension); compatible(type)) {
    return {.outcome = SniffOutcome::kDecided,
            .type = type,
            .signal = TypeSignal::kExtension};
  }
  if (const auto type = TypeFromContentType(hints.content_type);
      compatible(type)) {
    return {.outcome = SniffOutcome::kDecided,
            .type = type,
            .signal = TypeSignal::kContentType};
  }
  return {};
}

}
