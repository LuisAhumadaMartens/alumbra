#include "umbra/base/base.h"

#include <utility>
#include <vector>

namespace umbra::base::detail {

int SkeletonLinkProbe() { return 0; }

int SeedUseAfterMove(){std::vector<int> original{1};const auto moved=std::move(original);return original.front()+moved.front();}

}
