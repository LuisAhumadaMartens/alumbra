#include "umbra/ads/ad_manager.h"

int main() {
  const auto cue = umbra::ads::ResolveOffset(
      {.kind = umbra::ads::OffsetKind::kPercentage, .value = 50.0},
      penumbra::TimeDelta::FromSeconds(4));
  return cue.has_value() && cue->InMicroseconds() == 2'000'000 ? 0 : 1;
}
