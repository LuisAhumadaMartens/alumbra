#include <cstdint>

#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

bool ReadMaxAttempts(const umbra::settings::Store& store) {
#ifdef ALUMBRA_WRONG_TYPE
  return store.Get<bool>(umbra::settings::kNetworkRetryMaxAttempts);
#else
  return store.Get<std::int64_t>(umbra::settings::kNetworkRetryMaxAttempts) !=
         0;
#endif
}
