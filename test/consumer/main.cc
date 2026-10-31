#include "penumbra/interface/interface.h"
#include "umbra/base/base.h"

#ifndef PENUMBRA_INTERFACE_INTERFACE_H_
#error "The installed Penumbra interface header was not included."
#endif

static_assert(__cplusplus >= 202002L);

int main() { return umbra::base::detail::SkeletonLinkProbe(); }
