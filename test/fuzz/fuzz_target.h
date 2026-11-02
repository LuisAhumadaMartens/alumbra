#ifndef TEST_FUZZ_FUZZ_TARGET_H_
#define TEST_FUZZ_FUZZ_TARGET_H_

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

#endif
