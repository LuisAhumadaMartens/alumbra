#ifndef UMBRA_BASE_SIP_HASH_H_
#define UMBRA_BASE_SIP_HASH_H_

#include <array>
#include <cstdint>
#include <string_view>

namespace umbra::base::internal {

[[nodiscard]] std::uint64_t SipHash24(std::string_view input,
                                      std::array<std::uint64_t, 2> key);

}

#endif
