#ifndef TEST_UMBRA_UI_VIEW_MODEL_JSON_H_
#define TEST_UMBRA_UI_VIEW_MODEL_JSON_H_

#include <string>
#include <vector>

#include "umbra/ui/effect.h"
#include "umbra/ui/view_model.h"

namespace umbra::ui {

std::string ToJson(const ViewModel& view_model,
                   const std::vector<SequencedEffect>& effects);

}

#endif
