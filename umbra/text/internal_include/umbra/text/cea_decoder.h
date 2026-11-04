#ifndef UMBRA_TEXT_CEA_DECODER_H_
#define UMBRA_TEXT_CEA_DECODER_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace umbra::text {

struct CaptionSnapshot {
  uint8_t service = 0;
  bool digital = false;
  uint8_t row = 0;
  uint8_t column = 0;
  uint8_t rows = 15;
  uint8_t columns = 32;
  double time = 0.0;
  std::string text;
};

class CeaDecoder {
 public:
  CeaDecoder();
  CeaDecoder(const CeaDecoder&) = delete;
  CeaDecoder& operator=(const CeaDecoder&) = delete;
  ~CeaDecoder();

  [[nodiscard]] std::vector<CaptionSnapshot> Push(
      std::span<const uint8_t> cc_data, double presentation_time);
  void Reset();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}

#endif
