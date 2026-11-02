#ifndef TEST_PENUMBRA_INTERFACE_FAKE_HARNESS_H_
#define TEST_PENUMBRA_INTERFACE_FAKE_HARNESS_H_

#include <vector>

#include "penumbra_conformance/port_harness.h"

namespace penumbra::test {

std::vector<conformance::PortCase> FakePortCases();
std::vector<conformance::PortCase> BrokenFakePortCases();

}

#endif
