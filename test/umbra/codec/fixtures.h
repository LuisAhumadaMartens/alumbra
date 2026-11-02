#ifndef TEST_UMBRA_CODEC_FIXTURES_H_
#define TEST_UMBRA_CODEC_FIXTURES_H_

#include <string_view>

namespace umbra::codec::test_data {

inline constexpr std::string_view kAvcC1080 =
    "01640028ffe1001b67640028acd940780227e5c044000003000400000300c03c60c65801"
    "000668ebe3cb22c0fdf8f800";
inline constexpr std::string_view kAvcC1078 =
    "01640028ffe1001b67640028acd940780227a9b011000003000100000300300f18319601"
    "000668ebe3cb22c0fdf8f800";
inline constexpr std::string_view kHvcC1080Main =
    "01016000000090000000000078f000fcfdf8f800000f03a00001001840010c01ffff0160"
    "00000300900000030000030078959809a10001002a420101016000000300900000030000"
    "030078a003c08010e596566924caf0168080000003008000000c04a2000100074401c172"
    "b46240";
inline constexpr std::string_view kHvcC1080Main10 =
    "01022000000090000000000078f000fcfdfafa00000f03a00001001840010c01ffff0220"
    "00000300900000030000030078959809a10001002c420101022000000300900000030000"
    "030078a003c08010e4d96566924caf016808000003000800000300c040a2000100074401"
    "c172b46240";
inline constexpr std::string_view kVpcC = "010000000015820202020000";
inline constexpr std::string_view kAv1C = "81010c000a0b0000000cc4ff6736be4010";
inline constexpr std::string_view kAv1C10 =
    "81014c000a0b0000000cc4ff6736be5010";
inline constexpr std::string_view kAscAacMono = "118856e500";
inline constexpr std::string_view kAscAac51 = "11b056e500";
inline constexpr std::string_view kDOps = "000101380000bb80000000";
inline constexpr std::string_view kDac3 = "1008c0";
inline constexpr std::string_view kDec3 = "0300200200";
inline constexpr std::string_view kDfLa =
    "0000000080000022120012000000c80004f10bb800f000002580412ff86260626166bdec"
    "bc206adb1573";

}

#endif
