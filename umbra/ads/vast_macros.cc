#include "umbra/ads/vast_macros.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace umbra::ads {

std::string ExpandVastMacros(std::string_view input,
                             const VastMacroValues& values) {
  std::string output;
  output.reserve(input.size());
  std::size_t position = 0;
  while (position < input.size()) {
    const std::size_t start = input.find('[', position);
    if (start == std::string_view::npos) {
      output.append(input.substr(position));
      break;
    }
    output.append(input.substr(position, start - position));
    const std::size_t end = input.find(']', start + 1);
    if (end == std::string_view::npos) {
      output.append(input.substr(start));
      break;
    }
    const std::string_view name = input.substr(start + 1, end - start - 1);
    const auto found = values.find(std::string(name));
    if (found == values.end()) {
      output.append(input.substr(start, end - start + 1));
    } else {
      output.append(found->second);
    }
    position = end + 1;
  }
  return output;
}

}
