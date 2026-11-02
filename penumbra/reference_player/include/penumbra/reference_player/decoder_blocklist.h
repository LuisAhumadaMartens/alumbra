#ifndef PENUMBRA_REFERENCE_PLAYER_DECODER_BLOCKLIST_H_
#define PENUMBRA_REFERENCE_PLAYER_DECODER_BLOCKLIST_H_

#include <span>
#include <string>
#include <string_view>

#include "penumbra/interface/stream_info.h"

namespace penumbra {

struct BlocklistEntry {
  std::string_view id;
  std::string_view decoder;
  std::string_view codec;
  std::string_view port;
  std::string_view os_min;
  std::string_view os_max;
  std::string_view device_model;
  std::string_view gpu_vendor;
  std::string_view gpu_device;
  std::string_view driver_min;
  std::string_view driver_max;
  std::string_view reason;
  std::string_view evidence;
  std::string_view since;
};

struct DeviceProfile {
  std::string port;
  std::string os_version;
  std::string device_model;
  std::string gpu_vendor;
  std::string gpu_device;
  std::string driver_version;
};

const BlocklistEntry* FindBlocklistEntry(
    std::span<const BlocklistEntry> entries, std::string_view decoder,
    CodecId codec, const DeviceProfile& device,
    std::span<const std::string> ignored_entries);

}

#endif
