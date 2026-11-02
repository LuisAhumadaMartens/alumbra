#include "umbra/observability/trace.h"

int main() {
  const int probe_value = 1;
#if ALUMBRA_TRACE_ENABLED
  umbra::observability::SetTraceCategoryMask(0x3FFF);
#endif
  ALUMBRA_TRACE(kNet, "trace_probe_unique_scope");
  ALUMBRA_TRACE_INSTANT(kManifest, "trace_probe_unique_instant");
  ALUMBRA_TRACE_COUNTER(kAbr, "trace_probe_unique_counter", probe_value);
#if ALUMBRA_TRACE_ENABLED
  return umbra::observability::FormatTrace(umbra::observability::DumpTrace())
                 .empty()
             ? 1
             : 0;
#else
  return 0;
#endif
}
