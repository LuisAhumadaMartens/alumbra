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

  alumbra_track_handle track_handle = 0;
  const char track_label[] = "English";
  const char track_language[] = "en";
  if (alumbra_player_add_text_track(player_handle, ALUMBRA_TEXT_TRACK_SUBTITLES,
                                    track_label, sizeof(track_label) - 1,
                                    track_language, sizeof(track_language) - 1,
                                    &track_handle) != ALUMBRA_STATUS_OK ||
      track_handle == 0) {
    return 1;
  }
  size_t track_count = 0;
  uint32_t track_kind = 0;
  uint32_t track_mode = 0;
  if (alumbra_player_text_track_count(player_handle, &track_count) !=
          ALUMBRA_STATUS_OK ||
      track_count != 1 ||
      alumbra_text_track_get_kind(track_handle, &track_kind) !=
          ALUMBRA_STATUS_OK ||
      track_kind != ALUMBRA_TEXT_TRACK_SUBTITLES ||
      alumbra_text_track_set_mode(track_handle, ALUMBRA_TEXT_TRACK_SHOWING) !=
          ALUMBRA_STATUS_OK ||
      alumbra_text_track_get_mode(track_handle, &track_mode) !=
          ALUMBRA_STATUS_OK ||
      track_mode != ALUMBRA_TEXT_TRACK_SHOWING) {
    return 1;
  }
  char returned_label[16] = {0};
  size_t returned_label_size = sizeof(returned_label);
  if (alumbra_text_track_get_label(track_handle, returned_label,
                                   &returned_label_size) != ALUMBRA_STATUS_OK ||
      returned_label_size != sizeof(track_label) - 1 ||
      returned_label[0] != 'E' ||
      alumbra_text_track_release(track_handle) != ALUMBRA_STATUS_OK ||
      alumbra_text_track_release(track_handle) !=
          ALUMBRA_STATUS_INVALID_HANDLE) {
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
  alumbra_statistics statistics = {.struct_size = sizeof(alumbra_statistics)};
  if (alumbra_player_get_current_time(player_handle, &current_time) !=
          ALUMBRA_STATUS_OK ||
      current_time != 1.25 ||
      alumbra_player_get_ready_state(player_handle, &ready_state) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_network_state(player_handle, &network_state) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_statistics(player_handle, &statistics) !=
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

  uint8_t state_flag = 0;
  uint32_t preload = 0;
  double duration = 0.0;
  uint32_t can_play = 0;
  const char media_type[] = "video/mp4";
  size_t range_count = 0;
  if (alumbra_player_get_paused(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_ended(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_autoplay(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_set_autoplay(player_handle, 1) != ALUMBRA_STATUS_OK ||
      alumbra_player_get_autoplay(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      state_flag != 1 ||
      alumbra_player_set_loop(player_handle, 1) != ALUMBRA_STATUS_OK ||
      alumbra_player_get_loop(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      state_flag != 1 ||
      alumbra_player_get_duration(player_handle, &duration) !=
          ALUMBRA_STATUS_OK ||
      duration != 3.0 ||
      alumbra_player_get_preload(player_handle, &preload) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_set_preload(player_handle, ALUMBRA_PRELOAD_METADATA) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_default_playback_rate(player_handle, &playback_rate) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_set_default_playback_rate(player_handle, 1.25) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_get_preserves_pitch(player_handle, &state_flag) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_set_preserves_pitch(player_handle, 0) !=
          ALUMBRA_STATUS_OK ||
      alumbra_player_can_play_type(player_handle, media_type,
                                   sizeof(media_type) - 1,
                                   &can_play) != ALUMBRA_STATUS_OK ||
      can_play > 2 ||
      alumbra_player_get_time_ranges(player_handle,
                                     ALUMBRA_TIME_RANGES_BUFFERED,
                                     &range_count) != ALUMBRA_STATUS_OK) {
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
  event.struct_size = offsetof(alumbra_event, error_code);
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
      if (event.error_code == 0) {
        return 1;
      }
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
