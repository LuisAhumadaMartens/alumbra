#ifndef UMBRA_UI_REDUCE_H_
#define UMBRA_UI_REDUCE_H_

#include <string>
#include <vector>

#include "umbra/ui/effect.h"
#include "umbra/ui/input.h"
#include "umbra/ui/state.h"
#include "umbra/ui/types.h"

namespace umbra::ui {

struct Step {
  UiState state;
  std::vector<Effect> effects;
};

[[nodiscard]] UiState InitialState();
[[nodiscard]] Step Reduce(const UiState& previous, const UiConfig& config,
                          Micros now_us, const Input& input);
[[nodiscard]] std::vector<std::string> CheckInvariants(const UiState& state,
                                                       const UiConfig& config);

}

#endif
