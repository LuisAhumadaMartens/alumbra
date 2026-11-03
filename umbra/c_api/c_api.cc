#include "umbra/c_api/c_api.h"

#include <cstddef>
#include <cstdint>

extern "C" {

uint32_t alumbra_c_api_version(void) { return ALUMBRA_C_API_VERSION; }

alumbra_status alumbra_negotiate_version(alumbra_version_info* info) {
  if (info == nullptr || info->struct_size < sizeof(alumbra_version_info)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  info->supported_version = ALUMBRA_C_API_VERSION;
  return info->requested_version == ALUMBRA_C_API_VERSION
             ? ALUMBRA_STATUS_OK
             : ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_create(const alumbra_player_config* config,
                                     alumbra_handle* player) {
  if (config == nullptr || player == nullptr ||
      config->struct_size < sizeof(alumbra_player_config)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  *player = 0;
  return ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_destroy(alumbra_handle player) {
  return player == 0 ? ALUMBRA_STATUS_INVALID_HANDLE
                     : ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_set_source(alumbra_handle player,
                                         const char* source,
                                         size_t source_size) {
  if (player == 0) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (source == nullptr || source_size == 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  return ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_load(alumbra_handle player) {
  return player == 0 ? ALUMBRA_STATUS_INVALID_HANDLE
                     : ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_play(alumbra_handle player) {
  return player == 0 ? ALUMBRA_STATUS_INVALID_HANDLE
                     : ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_pause(alumbra_handle player) {
  return player == 0 ? ALUMBRA_STATUS_INVALID_HANDLE
                     : ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_player_poll_event(alumbra_handle player,
                                         alumbra_event* event,
                                         uint8_t* available) {
  if (player == 0) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (event == nullptr || available == nullptr ||
      event->struct_size < sizeof(alumbra_event)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  *available = 0;
  return ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_settings_get(alumbra_handle player, const char* key,
                                    size_t key_size, char* value,
                                    size_t* value_size) {
  if (player == 0) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (key == nullptr || key_size == 0 || value_size == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  const size_t capacity = *value_size;
  *value_size = 0;
  if (value != nullptr && capacity > 0) {
    *value = '\0';
  }
  return ALUMBRA_STATUS_UNSUPPORTED;
}

alumbra_status alumbra_settings_set(alumbra_handle player, const char* key,
                                    size_t key_size, const char* value,
                                    size_t value_size) {
  if (player == 0) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (key == nullptr || key_size == 0 || value == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  static_cast<void>(value_size);
  return ALUMBRA_STATUS_UNSUPPORTED;
}
}
