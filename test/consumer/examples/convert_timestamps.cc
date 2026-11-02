#include <optional>

#include "penumbra/interface/media_time.h"
#include "penumbra/interface/rational.h"

int main() {
  constexpr penumbra::Rational kMpegTs = penumbra::Rational::Of(1, 90'000);
  constexpr penumbra::Rational kAudio = penumbra::Rational::Of(1, 48'000);
  const penumbra::Timestamp pts{.value = 900'900, .time_base = kMpegTs};

  const std::optional<penumbra::MediaTime> position =
      penumbra::ToMediaTime(pts, penumbra::Rounding::kNearest);
  if (!position.has_value()) {
    return 1;
  }
  const penumbra::TimeDelta offset =
      *position - penumbra::MediaTime::FromSeconds(10);
  const penumbra::TimeDelta unknown =
      penumbra::MediaTime::Unknown() - *position;
  const std::optional<penumbra::Timestamp> audio =
      penumbra::Rescale(pts, kAudio, penumbra::Rounding::kDown);

  const bool ok = position->InMicroseconds() == 10'010'000 &&
                  offset == penumbra::TimeDelta::FromMilliseconds(10) &&
                  unknown.is_unknown() && audio.has_value() &&
                  audio->value == 480'480;
  return ok ? 0 : 1;
}
