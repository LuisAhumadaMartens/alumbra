#include "umbra/c_api/c_api.h"

static uint32_t notification_count;
static alumbra_handle player_handle;

static void Notify(void* user_data) {
  uint32_t* count = (uint32_t*)user_data;
  ++*count;
}

int RunCConsumerBegin(void) {
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
  if (alumbra_negotiate_version(&info) != ALUMBRA_STATUS_UNSUPPORTED ||
      info.supported_version != ALUMBRA_C_API_VERSION) {
    return 1;
  }

  alumbra_player_config config = {
      .struct_size = sizeof(alumbra_player_config),
      .notify = Notify,
      .user_data = &notification_count,
  };
  player_handle = 0;
  config.struct_size = sizeof(config) - 1;
  if (alumbra_player_create(&config, &player_handle) !=
          ALUMBRA_STATUS_INVALID_ARGUMENT ||
      player_handle != 0) {
    return 1;
  }
  config.struct_size = sizeof(config);
  if (alumbra_player_create(&config, &player_handle) != ALUMBRA_STATUS_OK ||
      player_handle == 0) {
    return 1;
  }

  const char source[] = "fake://media";
  if (alumbra_player_set_source(player_handle, source, sizeof(source) - 1) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_load(player_handle) != ALUMBRA_STATUS_OK ||
      alumbra_player_play(player_handle) != ALUMBRA_STATUS_OK ||
      alumbra_player_set_current_time(player_handle, 1.25) !=
          ALUMBRA_STATUS_OK) {
    return 1;
  }

  char source_copy[32] = {0};
  size_t source_copy_size = sizeof(source_copy);
  uint8_t seeking = 0;
  if (alumbra_player_get_source(player_handle, source_copy,
                                &source_copy_size) != ALUMBRA_STATUS_OK ||
      source_copy_size != sizeof(source) - 1 ||
      alumbra_player_get_seeking(player_handle, &seeking) !=
          ALUMBRA_STATUS_OK) {
    return 1;
  }

  double current_time = 0.0;
  uint32_t ready_state = 0;
  uint32_t network_state = 0;
  if (alumbra_player_get_current_time(player_handle, &current_time) !=
          ALUMBRA_STATUS_OK ||
      current_time != 1.25 ||
      alumbra_player_get_ready_state(player_handle, &ready_state) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_network_state(player_handle, &network_state) !=
          ALUMBRA_STATUS_OK) {
    return 1;
  }

  double volume = 0.0;
  uint8_t muted = 0;
  double playback_rate = 0.0;
  if (alumbra_player_set_volume(player_handle, 0.5) != ALUMBRA_STATUS_OK ||
      alumbra_player_get_volume(player_handle, &volume) != ALUMBRA_STATUS_OK ||
      volume != 0.5 ||
      alumbra_player_set_muted(player_handle, 1) != ALUMBRA_STATUS_OK ||
      alumbra_player_get_muted(player_handle, &muted) != ALUMBRA_STATUS_OK ||
      muted != 1 ||
      alumbra_player_set_playback_rate(player_handle, 1.5) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_playback_rate(player_handle, &playback_rate) !=
          ALUMBRA_STATUS_OK ||
      playback_rate != 1.5 ||
      alumbra_player_pause(player_handle) != ALUMBRA_STATUS_OK) {
    return 1;
  }

  if (alumbra_settings_set(player_handle,
                           "player.events.timeupdate_interval_ms",
                           sizeof("player.events.timeupdate_interval_ms") - 1,
                           "100", 3) != ALUMBRA_STATUS_OK) {
    return 1;
  }
  char setting_value[16] = {0};
  size_t setting_size = sizeof(setting_value);
  if (alumbra_settings_get(player_handle,
                           "player.events.timeupdate_interval_ms",
                           sizeof("player.events.timeupdate_interval_ms") - 1,
                           setting_value, &setting_size) != ALUMBRA_STATUS_OK ||
      setting_size != 3 || setting_value[0] != '1' || setting_value[1] != '0' ||
      setting_value[2] != '0') {
    return 1;
  }
  return 0;
}

int RunCConsumerFinish(void) {
  alumbra_event event = {.struct_size = sizeof(alumbra_event)};
  uint8_t available = 0;
  event.struct_size = sizeof(event) - 1;
  if (alumbra_player_poll_event(player_handle, &event, &available) !=
      ALUMBRA_STATUS_INVALID_ARGUMENT) {
    return 1;
  }
  event.struct_size = sizeof(event);
  int saw_error_event = 0;
  for (size_t index = 0; index < 32; ++index) {
    if (alumbra_player_poll_event(player_handle, &event, &available) !=
        ALUMBRA_STATUS_OK) {
      return 1;
    }
    if (available == 0) {
      break;
    }
    if (event.type == ALUMBRA_EVENT_ERROR) {
      saw_error_event = 1;
    }
  }
  if (!saw_error_event || notification_count == 0) {
    return 1;
  }

  alumbra_error_info error = {.struct_size = sizeof(alumbra_error_info)};
  size_t message_size = 0;
  if (alumbra_player_get_error(player_handle, &error, 0, &message_size) !=
          ALUMBRA_STATUS_BUFFER_TOO_SMALL ||
      error.code == 0 || message_size == 0) {
    return 1;
  }
  char message[256] = {0};
  size_t message_capacity = sizeof(message);
  if (alumbra_player_get_error(player_handle, &error, message,
                               &message_capacity) != ALUMBRA_STATUS_OK) {
    return 1;
  }

  if (alumbra_player_destroy(player_handle) != ALUMBRA_STATUS_OK ||
      alumbra_player_destroy(player_handle) != ALUMBRA_STATUS_INVALID_HANDLE) {
    return 1;
  }
  return 0;
}
