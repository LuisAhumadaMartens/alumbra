#ifndef UMBRA_PLAYER_MEDIA_ERROR_H_
#define UMBRA_PLAYER_MEDIA_ERROR_H_

#include <string>

#include "umbra/base/error_detail.h"

namespace umbra::player {

enum class MediaErrorCode : int {
  kAborted = 1,
  kNetwork = 2,
  kDecode = 3,
  kSrcNotSupported = 4,
};

class MediaError {
 public:
  MediaError(MediaErrorCode code, base::ErrorDetail detail);

  [[nodiscard]] MediaErrorCode code() const { return code_; }
  [[nodiscard]] const std::string& message() const { return message_; }
  [[nodiscard]] const base::ErrorDetail& detail() const { return detail_; }

 private:
  MediaErrorCode code_;
  std::string message_;
  base::ErrorDetail detail_;
};

}

#endif
