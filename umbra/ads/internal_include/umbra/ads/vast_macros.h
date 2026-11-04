#ifndef UMBRA_ADS_VAST_MACROS_H_
#define UMBRA_ADS_VAST_MACROS_H_

#include <string>
#include <string_view>
#include <unordered_map>

namespace umbra::ads {

using VastMacroValues = std::unordered_map<std::string, std::string>;

std::string ExpandVastMacros(std::string_view input,
                             const VastMacroValues& values);

}

#endif
