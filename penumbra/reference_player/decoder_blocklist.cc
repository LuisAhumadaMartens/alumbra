#include "penumbra/reference_player/decoder_blocklist.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "penumbra/interface/stream_info.h"

namespace penumbra {
namespace {

std::optional<std::vector<uint64_t>> ParseVersion(std::string_view text) {
  std::vector<uint64_t> parts;
  uint64_t current = 0;
  bool in_part = false;
  for (const char character : text) {
    if (character >= '0' && character <= '9') {
      current = (current * 10) + static_cast<uint64_t>(character - '0');
      in_part = true;
    } else if (character == '.' && in_part) {
      parts.push_back(current);
      current = 0;
      in_part = false;
    } else {
      return std::nullopt;
    }
  }
  if (!in_part) {
    return std::nullopt;
  }
  parts.push_back(current);
  return parts;
}

int CompareVersions(const std::vector<uint64_t>& left,
                    const std::vector<uint64_t>& right) {
  const size_t count = std::max(left.size(), right.size());
  for (size_t index = 0; index < count; ++index) {
    const uint64_t a = index < left.size() ? left[index] : 0;
    const uint64_t b = index < right.size() ? right[index] : 0;
    if (a != b) {
      return a < b ? -1 : 1;
    }
  }
  return 0;
}

bool TextMatches(std::string_view expected, std::string_view actual) {
  return expected.empty() || expected == actual;
}

bool RangeMatches(std::string_view low, std::string_view high,
                  std::string_view actual) {
  if (low.empty() && high.empty()) {
    return true;
  }
  const auto value = ParseVersion(actual);
  if (!value.has_value()) {
    return false;
  }
  if (!low.empty()) {
    const auto bound = ParseVersion(low);
    if (!bound.has_value() || CompareVersions(*value, *bound) < 0) {
      return false;
    }
  }
  if (!high.empty()) {
    const auto bound = ParseVersion(high);
    if (!bound.has_value() || CompareVersions(*value, *bound) > 0) {
      return false;
    }
  }
  return true;
}

bool Matches(const BlocklistEntry& entry, std::string_view decoder,
             CodecId codec, const DeviceProfile& device) {
  return entry.decoder == decoder &&
         (entry.codec.empty() || entry.codec == CodecName(codec)) &&
         TextMatches(entry.port, device.port) &&
         TextMatches(entry.device_model, device.device_model) &&
         TextMatches(entry.gpu_vendor, device.gpu_vendor) &&
         TextMatches(entry.gpu_device, device.gpu_device) &&
         RangeMatches(entry.os_min, entry.os_max, device.os_version) &&
         RangeMatches(entry.driver_min, entry.driver_max,
                      device.driver_version);
}

}

const BlocklistEntry* FindBlocklistEntry(
    std::span<const BlocklistEntry> entries, std::string_view decoder,
    CodecId codec, const DeviceProfile& device,
    std::span<const std::string> ignored_entries) {
  for (const auto& entry : entries) {
    if (std::ranges::find(ignored_entries, entry.id) != ignored_entries.end()) {
      continue;
    }
    if (Matches(entry, decoder, codec, device)) {
      return &entry;
    }
  }
  return nullptr;
}

}
