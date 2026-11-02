#include <vector>

#include "fake_harness.h"
#include "penumbra_conformance/conformance.h"
#include "penumbra_conformance/port_harness.h"

namespace penumbra::test {
namespace {

const std::vector<conformance::PortCase>& Cases() {
  static const std::vector<conformance::PortCase> cases = BrokenFakePortCases();
  return cases;
}

}

ALUMBRA_INSTANTIATE_PORT_CONFORMANCE(Conformance, Cases());

}
