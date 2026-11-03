#include "umbra/c_api/c_api.h"

int main(void) {
  alumbra_version_info info = {
      .struct_size = sizeof(alumbra_version_info),
      .requested_version = ALUMBRA_C_API_VERSION,
      .supported_version = 0,
  };
  if (alumbra_c_api_version() != ALUMBRA_C_API_VERSION ||
      alumbra_negotiate_version(&info) != ALUMBRA_STATUS_OK ||
      info.supported_version != ALUMBRA_C_API_VERSION) {
    return 1;
  }
  info.struct_size = sizeof(info) - 1;
  if (alumbra_negotiate_version(&info) != ALUMBRA_STATUS_INVALID_ARGUMENT) {
    return 1;
  }
  info.struct_size = sizeof(info);
  info.requested_version = ALUMBRA_C_API_VERSION + 1;
  return alumbra_negotiate_version(&info) == ALUMBRA_STATUS_UNSUPPORTED &&
                 info.supported_version == ALUMBRA_C_API_VERSION
             ? 0
             : 1;
}
