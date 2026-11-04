#include "umbra/text/cea_decoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace umbra::text {
namespace {

constexpr size_t kRows = 15;
constexpr size_t kColumns = 32;
constexpr size_t kWindows = 8;
constexpr size_t kMaxPacketBytes = 128;
using Screen = std::vector<std::vector<uint32_t>>;

Screen MakeScreen() { return {kRows, std::vector<uint32_t>(kColumns, ' ')}; }

void Clear(Screen& screen) {
  for (auto& row : screen) {
    std::ranges::fill(row, ' ');
  }
}

uint32_t Map608(uint8_t value) {
  if (value >= 0x20 && value <= 0x7F) {
    switch (value) {
      case 0x2A:
        return 0x00E1;
      case 0x5C:
        return 0x00E9;
      case 0x5E:
        return 0x00ED;
      case 0x5F:
        return 0x00F3;
      case 0x60:
        return 0x00FA;
      case 0x7B:
        return 0x00E7;
      case 0x7C:
        return 0x00F7;
      case 0x7D:
        return 0x00D1;
      case 0x7E:
        return 0x00F1;
      case 0x7F:
        return 0x2588;
      default:
        return value;
    }
  }
  return 0;
}

uint32_t MapSpecial608(uint8_t value) {
  const std::vector<uint32_t> special = {
      0x00E1, 0x00E9, 0x00ED, 0x00F3, 0x00FA, 0x00E7, 0x00F7, 0x00D1,
      0x00F1, 0x2588, 0x00AE, 0x00B0, 0x00BD, 0x00BF, 0x2122, 0x00A2};
  return value >= 0x30 && value <= 0x3F ? special[value - 0x30] : 0;
}

uint32_t MapExtended608(uint8_t value) {
  const std::vector<uint32_t> extended = {
      0x00C1, 0x00C9, 0x00D3, 0x00DA, 0x00DC, 0x00FC, 0x2018, 0x00A1,
      0x002A, 0x2019, 0x2014, 0x00A9, 0x2120, 0x2022, 0x201C, 0x201D,
      0x00C0, 0x00C2, 0x00C7, 0x00C8, 0x00CA, 0x00CB, 0x00EB, 0x00CE,
      0x00CF, 0x00EF, 0x00D4, 0x00D9, 0x00F9, 0x00DB, 0x00AB, 0x00BB};
  return value >= 0x20 && value <= 0x3F ? extended[value - 0x20] : 0;
}

std::string Utf8(uint32_t value) {
  std::string text;
  if (value <= 0x7F) {
    text.push_back(static_cast<char>(value));
  } else if (value <= 0x7FF) {
    text.push_back(static_cast<char>(0xC0 | (value >> 6)));
    text.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    text.push_back(static_cast<char>(0xE0 | (value >> 12)));
    text.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
  return text;
}

std::string ScreenText(const Screen& screen) {
  std::string text;
  for (const auto& row : screen) {
    size_t last = row.size();
    while (last > 0 && row[last - 1] == ' ') {
      --last;
    }
    if (last == 0) {
      continue;
    }
    if (!text.empty()) {
      text.push_back('\n');
    }
    for (size_t column = 0; column < last; ++column) {
      text += Utf8(row[column]);
    }
  }
  return text;
}

struct Channel {
  Screen displayed = MakeScreen();
  Screen non_displayed = MakeScreen();
  uint8_t row = 14;
  uint8_t column = 0;
  uint8_t roll_rows = 2;
  uint8_t last_a = 0;
  uint8_t last_b = 0;
  bool last_control = false;
  bool roll_up = false;
  bool paint_on = false;
};

struct Window {
  bool defined = false;
  bool visible = false;
  uint8_t row = 0;
  uint8_t column = 0;
  uint8_t rows = 1;
  uint8_t columns = 32;
  std::vector<uint32_t> cells;
};

}

class CeaDecoder::Impl {
 public:
  Impl() { Reset(); }

  void Reset() {
    channels_.assign(4, Channel{});
    for (Channel& channel : channels_) {
      Clear(channel.displayed);
      Clear(channel.non_displayed);
    }
    windows_.assign(kWindows, Window{});
    packet_.clear();
    packet_size_ = 0;
    active_window_ = 0;
  }

  std::vector<CaptionSnapshot> Push(std::span<const uint8_t> data,
                                    double time) {
    std::vector<CaptionSnapshot> output;
    if (data.size() % 3 != 0) {
      return output;
    }
    for (size_t offset = 0; offset < data.size(); offset += 3) {
      const uint8_t flags = data[offset];
      if ((flags & 0x04) == 0) {
        continue;
      }
      const uint8_t type = flags & 0x03;
      if (type < 2) {
        Decode608(type, data[offset + 1] & 0x7F, data[offset + 2] & 0x7F, time,
                  output);
      } else {
        Decode708(type, data[offset + 1], data[offset + 2], time, output);
      }
    }
    return output;
  }

 private:
  void Emit608(size_t index, double time,
               std::vector<CaptionSnapshot>& output) {
    CaptionSnapshot cue;
    cue.service = static_cast<uint8_t>(index + 1);
    cue.time = time;
    cue.text = ScreenText(channels_[index].displayed);
    output.push_back(std::move(cue));
  }

  static void Add608Character(Channel& channel, uint32_t character) {
    Screen& screen = channel.paint_on || channel.roll_up
                         ? channel.displayed
                         : channel.non_displayed;
    if (channel.column < kColumns) {
      screen[channel.row][channel.column++] = character;
    }
  }

  void Process608ControlCode(size_t index, Channel& channel, uint8_t second,
                             double time,
                             std::vector<CaptionSnapshot>& output) {
    switch (second) {
      case 0x20:
        channel.roll_up = false;
        channel.paint_on = false;
        break;
      case 0x21:
        if (channel.column > 0) {
          --channel.column;
          channel.displayed[channel.row][channel.column] = ' ';
        }
        break;
      case 0x24:
        std::ranges::fill(
            channel.displayed[channel.row] | std::views::drop(channel.column),
            ' ');
        break;
      case 0x25:
      case 0x26:
      case 0x27:
        channel.roll_up = true;
        channel.paint_on = false;
        channel.roll_rows = static_cast<uint8_t>(second - 0x23);
        break;
      case 0x29:
        channel.paint_on = true;
        channel.roll_up = false;
        break;
      case 0x2C:
        Clear(channel.displayed);
        Emit608(index, time, output);
        break;
      case 0x2D:
        RollUp608(index, channel, time, output);
        break;
      case 0x2E:
        Clear(channel.non_displayed);
        break;
      case 0x2F:
        std::swap(channel.displayed, channel.non_displayed);
        Emit608(index, time, output);
        break;
      default:
        break;
    }
  }

  void RollUp608(size_t index, Channel& channel, double time,
                 std::vector<CaptionSnapshot>& output) {
    if (!channel.roll_up) {
      return;
    }
    const size_t start = channel.row + 1 >= channel.roll_rows
                             ? channel.row + 1 - channel.roll_rows
                             : 0;
    for (size_t row = start; row < channel.row; ++row) {
      channel.displayed[row] = channel.displayed[row + 1];
    }
    std::ranges::fill(channel.displayed[channel.row], ' ');
    channel.column = 0;
    Emit608(index, time, output);
  }

  static void Handle608MiscControl(Channel& channel, uint8_t first,
                                   uint8_t second) {
    if (first >= 0x11 && first <= 0x17 && (second & 0x40) != 0) {
      channel.row = static_cast<uint8_t>(
          std::min<size_t>(14, ((first & 0x07) * 2) + ((second >> 5) & 1)));
      channel.column = (second & 0x10) != 0
                           ? static_cast<uint8_t>(((second >> 1) & 7) * 4)
                           : 0;
      return;
    }
    if (first == 0x11 && second >= 0x20 && second <= 0x2F) {
      Add608Character(channel, ' ');
    } else if (first == 0x17 && second >= 0x21 && second <= 0x23) {
      channel.column = static_cast<uint8_t>(
          std::min<size_t>(31, channel.column + second - 0x20));
    }
  }

  void Process608Control(size_t index, Channel& channel, uint8_t first,
                         uint8_t second, double time,
                         std::vector<CaptionSnapshot>& output) {
    const bool repeated = channel.last_control && channel.last_a == first &&
                          channel.last_b == second;
    channel.last_control = true;
    channel.last_a = first;
    channel.last_b = second;
    if (repeated) {
      return;
    }
    if (first >= 0x14) {
      Process608ControlCode(index, channel, second, time, output);
    } else {
      Handle608MiscControl(channel, first, second);
    }
  }

  static void Decode608Character(Channel& channel, uint8_t first,
                                 uint8_t second) {
    for (const uint8_t value : {first, second}) {
      if (value >= 0x20 && value <= 0x7F) {
        Add608Character(channel, Map608(value));
      }
    }
  }

  void Decode608(uint8_t field, uint8_t first, uint8_t second, double time,
                 std::vector<CaptionSnapshot>& output) {
    size_t index = static_cast<size_t>(field) * 2;
    const bool control = first >= 0x10 && first <= 0x1F;
    if (control && first >= 0x18) {
      ++index;
      first -= 8;
    }
    Channel& channel = channels_[index];
    if (first == 0x11 && second >= 0x30 && second <= 0x3F) {
      Add608Character(channel, MapSpecial608(second));
      channel.last_control = false;
      if (channel.paint_on || channel.roll_up) {
        Emit608(index, time, output);
      }
      return;
    }
    if (first == 0x12 || first == 0x13) {
      Decode608Extended(index, channel, second, time, output);
      return;
    }
    if (control) {
      Process608Control(index, channel, first, second, time, output);
      return;
    }
    channel.last_control = false;
    Decode608Character(channel, first, second);
    if (channel.paint_on || channel.roll_up) {
      Emit608(index, time, output);
    }
  }

  void Decode608Extended(size_t index, Channel& channel, uint8_t second,
                         double time, std::vector<CaptionSnapshot>& output) {
    if (second < 0x20 || second > 0x3F) {
      return;
    }
    Screen& screen = channel.paint_on || channel.roll_up
                         ? channel.displayed
                         : channel.non_displayed;
    if (channel.column > 0) {
      screen[channel.row][channel.column - 1] = MapExtended608(second);
    }
    if (channel.paint_on || channel.roll_up) {
      Emit608(index, time, output);
    }
    channel.last_control = false;
  }

  void Decode708(uint8_t type, uint8_t first, uint8_t second, double time,
                 std::vector<CaptionSnapshot>& output) {
    if (type == 3) {
      packet_.clear();
      packet_size_ = static_cast<size_t>(first & 0x3F);
      if (packet_size_ == 0) {
        packet_size_ = 64;
      }
      packet_size_ = std::min((packet_size_ * 2) - 1, kMaxPacketBytes);
      packet_.push_back(second);
      return;
    }
    if (type != 2 || packet_size_ == 0 || packet_.size() + 2 > packet_size_) {
      return;
    }
    packet_.push_back(first);
    packet_.push_back(second);
    if (packet_.size() == packet_size_) {
      DecodePacket(time, output);
      packet_.clear();
      packet_size_ = 0;
    }
  }

  void DecodePacket(double time, std::vector<CaptionSnapshot>& output) {
    size_t offset = 0;
    while (offset < packet_.size()) {
      const uint8_t header = packet_[offset++];
      uint8_t service = header >> 5;
      const size_t size = header & 0x1F;
      if (service == 7) {
        if (offset >= packet_.size()) {
          return;
        }
        service = packet_[offset++] & 0x3F;
      }
      if (service == 0 || size > packet_.size() - offset) {
        return;
      }
      DecodeService(service,
                    std::span<const uint8_t>(packet_).subspan(offset, size),
                    time, output);
      offset += size;
    }
  }

  bool Process708Command(uint8_t service, uint8_t code,
                         std::span<const uint8_t> data, size_t& offset,
                         double time, std::vector<CaptionSnapshot>& output) {
    if (code >= 0x20 && code <= 0x7F) {
      Append708(service, code, time, output);
      return true;
    }
    if (code >= 0x80 && code <= 0x87) {
      active_window_ = code & 7;
      return true;
    }
    if (code >= 0x88 && code <= 0x8C) {
      return Process708WindowMask(service, code, data, offset, time, output);
    }
    if (code >= 0x98 && code <= 0x9F) {
      return Define708Window(service, code, data, offset, time, output);
    }
    switch (code) {
      case 0x03:
        Emit708(service, time, output);
        return true;
      case 0x08:
        Append708(service, code, time, output);
        return true;
      case 0x0C:
        windows_.assign(kWindows, Window{});
        Emit708(service, time, output);
        return true;
      case 0x0D:
        Append708(service, code, time, output);
        return true;
      case 0x10:
        return Skip708Bytes(data, offset, 1);
      case 0x18:
        if (!Skip708Bytes(data, offset, 2)) {
          return false;
        }
        Append708(
            service,
            (static_cast<uint32_t>(data[offset - 2]) << 8) | data[offset - 1],
            time, output);
        return true;
      case 0x8F:
        windows_.assign(kWindows, Window{});
        Emit708(service, time, output);
        return true;
      default:
        return Process708ExtendedCommand(code, data, offset);
    }
  }

  bool Process708WindowMask(uint8_t service, uint8_t code,
                            std::span<const uint8_t> data, size_t& offset,
                            double time, std::vector<CaptionSnapshot>& output) {
    if (!Skip708Bytes(data, offset, 1)) {
      return false;
    }
    const uint8_t mask = data[offset - 1];
    for (size_t window = 0; window < kWindows; ++window) {
      if ((mask & (1U << window)) != 0) {
        Apply708WindowCommand(windows_[window], code);
      }
    }
    Emit708(service, time, output);
    return true;
  }

  static void Apply708WindowCommand(Window& window, uint8_t code) {
    if (code == 0x88 || code == 0x89) {
      window.cells.clear();
    }
    if (code == 0x8A || code == 0x89) {
      window.visible = false;
    }
    if (code == 0x8B) {
      window.visible = true;
    }
  }

  bool Define708Window(uint8_t service, uint8_t code,
                       std::span<const uint8_t> data, size_t& offset,
                       double time, std::vector<CaptionSnapshot>& output) {
    if (data.size() - offset < 6) {
      return false;
    }
    Window& window = windows_[code & 7];
    window.defined = true;
    window.visible = (data[offset] & 0x20) != 0;
    window.row = data[offset + 1] & 0x0F;
    window.column = data[offset + 2] & 0x3F;
    window.rows = static_cast<uint8_t>((data[offset + 3] & 0x0F) + 1);
    window.columns = static_cast<uint8_t>((data[offset + 4] & 0x3F) + 1);
    window.cells.clear();
    offset += 6;
    Emit708(service, time, output);
    return true;
  }

  static bool Skip708Bytes(std::span<const uint8_t> data, size_t& offset,
                           size_t count) {
    if (count > data.size() - offset) {
      return false;
    }
    offset += count;
    return true;
  }

  static bool Process708ExtendedCommand(uint8_t code,
                                        std::span<const uint8_t> data,
                                        size_t& offset) {
    size_t skip = 0;
    if (code >= 0x90 && code <= 0x91) {
      skip = 2;
    } else if (code >= 0x92 && code <= 0x97) {
      skip = 3;
    } else if (code == 0xA0) {
      skip = 4;
    } else if (code == 0x10) {
      skip = 1;
    }
    return Skip708Bytes(data, offset, skip);
  }

  void DecodeService(uint8_t service, std::span<const uint8_t> data,
                     double time, std::vector<CaptionSnapshot>& output) {
    size_t offset = 0;
    while (offset < data.size()) {
      const uint8_t code = data[offset++];
      if (!Process708Command(service, code, data, offset, time, output)) {
        return;
      }
    }
  }

  void Append708(uint8_t service, uint32_t value, double time,
                 std::vector<CaptionSnapshot>& output) {
    Window& window = windows_[active_window_];
    if (!window.defined) {
      window.defined = true;
      window.visible = true;
    }
    if (value == 0x08) {
      if (!window.cells.empty()) {
        window.cells.pop_back();
      }
    } else if (value == 0x0D) {
      window.cells.push_back('\n');
    } else if (value >= 0x20 && window.cells.size() < 1024) {
      window.cells.push_back(value);
    }
    Emit708(service, time, output);
  }

  void Emit708(uint8_t service, double time,
               std::vector<CaptionSnapshot>& output) {
    for (const Window& window : windows_) {
      if (!window.defined || !window.visible) {
        continue;
      }
      CaptionSnapshot cue;
      cue.service = service;
      cue.digital = true;
      cue.row = window.row;
      cue.column = window.column;
      cue.rows = window.rows;
      cue.columns = window.columns;
      cue.time = time;
      for (const uint32_t value : window.cells) {
        if (value == '\n') {
          cue.text.push_back('\n');
        } else {
          cue.text += Utf8(value);
        }
      }
      output.push_back(std::move(cue));
    }
  }

  std::vector<Channel> channels_;
  std::vector<Window> windows_;
  std::vector<uint8_t> packet_;
  size_t packet_size_ = 0;
  uint8_t active_window_ = 0;
};

CeaDecoder::CeaDecoder() : impl_(std::make_unique<Impl>()) {}
CeaDecoder::~CeaDecoder() = default;
std::vector<CaptionSnapshot> CeaDecoder::Push(std::span<const uint8_t> cc_data,
                                              double presentation_time) {
  return impl_->Push(cc_data, presentation_time);
}
void CeaDecoder::Reset() { impl_->Reset(); }

}
