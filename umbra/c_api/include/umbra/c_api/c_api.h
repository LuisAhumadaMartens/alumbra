#ifndef ALUMBRA_C_API_H_
#define ALUMBRA_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALUMBRA_C_API_VERSION 1u

typedef uint64_t alumbra_handle;
typedef uint64_t alumbra_track_handle;
typedef int32_t alumbra_status;

#define ALUMBRA_STATUS_OK ((alumbra_status)0)
#define ALUMBRA_STATUS_INVALID_ARGUMENT ((alumbra_status)1)
#define ALUMBRA_STATUS_INVALID_HANDLE ((alumbra_status)2)
#define ALUMBRA_STATUS_UNSUPPORTED ((alumbra_status)3)
#define ALUMBRA_STATUS_BUFFER_TOO_SMALL ((alumbra_status)4)
#define ALUMBRA_STATUS_WRONG_THREAD ((alumbra_status)5)

typedef struct alumbra_version_info {
  uint32_t struct_size;
  uint32_t requested_version;
  uint32_t supported_version;
} alumbra_version_info;

typedef struct alumbra_player_config {
  uint32_t struct_size;
  void (*notify)(void* user_data);
  void* user_data;
} alumbra_player_config;

typedef struct alumbra_event {
  uint32_t struct_size;
  uint32_t type;
  uint64_t sequence;
  double current_time;
  double duration;
  uint32_t error_code;
} alumbra_event;

typedef struct alumbra_error_info {
  uint32_t struct_size;
  uint32_t code;
  uint32_t message_size;
} alumbra_error_info;

typedef struct alumbra_statistics {
  uint32_t struct_size;
  uint32_t reserved;
  uint64_t present_mask;
  double average_initial_startup_time_s;
  uint64_t startup_failures;
  double playback_failure_percentage;
  double average_playback_stalled_count;
  double average_stalled_time_percentage;
  double average_playback_bitrate_kbps;
  double average_segment_bitrate_bps;
  uint64_t rendition_switch_count;
  uint64_t dropped_frames;
  double live_latency_ms;
} alumbra_statistics;

#define ALUMBRA_EVENT_LOADSTART 0
#define ALUMBRA_EVENT_PROGRESS 1
#define ALUMBRA_EVENT_SUSPEND 2
#define ALUMBRA_EVENT_ABORT 3
#define ALUMBRA_EVENT_ERROR 4
#define ALUMBRA_EVENT_EMPTIED 5
#define ALUMBRA_EVENT_STALLED 6
#define ALUMBRA_EVENT_LOADEDMETADATA 7
#define ALUMBRA_EVENT_LOADEDDATA 8
#define ALUMBRA_EVENT_CANPLAY 9
#define ALUMBRA_EVENT_CANPLAYTHROUGH 10
#define ALUMBRA_EVENT_PLAYING 11
#define ALUMBRA_EVENT_WAITING 12
#define ALUMBRA_EVENT_SEEKING 13
#define ALUMBRA_EVENT_SEEKED 14
#define ALUMBRA_EVENT_ENDED 15
#define ALUMBRA_EVENT_DURATIONCHANGE 16
#define ALUMBRA_EVENT_TIMEUPDATE 17
#define ALUMBRA_EVENT_PLAY 18
#define ALUMBRA_EVENT_PAUSE 19
#define ALUMBRA_EVENT_RATECHANGE 20
#define ALUMBRA_EVENT_RESIZE 21
#define ALUMBRA_EVENT_VOLUMECHANGE 22
#define ALUMBRA_EVENT_WARNING 23

#define ALUMBRA_TEXT_TRACK_SUBTITLES 0
#define ALUMBRA_TEXT_TRACK_CAPTIONS 1
#define ALUMBRA_TEXT_TRACK_DESCRIPTIONS 2
#define ALUMBRA_TEXT_TRACK_CHAPTERS 3
#define ALUMBRA_TEXT_TRACK_METADATA 4

#define ALUMBRA_TEXT_TRACK_DISABLED 0
#define ALUMBRA_TEXT_TRACK_HIDDEN 1
#define ALUMBRA_TEXT_TRACK_SHOWING 2

#define ALUMBRA_PRELOAD_NONE 0
#define ALUMBRA_PRELOAD_METADATA 1
#define ALUMBRA_PRELOAD_AUTO 2

#define ALUMBRA_TIME_RANGES_BUFFERED 0
#define ALUMBRA_TIME_RANGES_SEEKABLE 1

#define ALUMBRA_STATISTIC_AVERAGE_INITIAL_STARTUP_TIME 0
#define ALUMBRA_STATISTIC_STARTUP_FAILURES 1
#define ALUMBRA_STATISTIC_PLAYBACK_FAILURE_PERCENTAGE 2
#define ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_STALLED_COUNT 3
#define ALUMBRA_STATISTIC_AVERAGE_STALLED_TIME_PERCENTAGE 4
#define ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_BITRATE 5
#define ALUMBRA_STATISTIC_AVERAGE_SEGMENT_BITRATE 6
#define ALUMBRA_STATISTIC_RENDITION_SWITCH_COUNT 7
#define ALUMBRA_STATISTIC_DROPPED_FRAMES 8
#define ALUMBRA_STATISTIC_LIVE_LATENCY 9

uint32_t alumbra_c_api_version(void);
alumbra_status alumbra_negotiate_version(alumbra_version_info* info);
alumbra_status alumbra_player_create(const alumbra_player_config* config,
                                     alumbra_handle* player);
alumbra_status alumbra_player_destroy(alumbra_handle player);
alumbra_status alumbra_player_get_source(alumbra_handle player, char* source,
                                         size_t* source_size);
alumbra_status alumbra_player_set_source(alumbra_handle player,
                                         const char* source,
                                         size_t source_size);
alumbra_status alumbra_player_load(alumbra_handle player);
alumbra_status alumbra_player_play(alumbra_handle player);
alumbra_status alumbra_player_pause(alumbra_handle player);
alumbra_status alumbra_player_fast_seek(alumbra_handle player, double seconds);
alumbra_status alumbra_player_can_play_type(alumbra_handle player,
                                            const char* type, size_t type_size,
                                            uint32_t* result);
alumbra_status alumbra_player_get_paused(alumbra_handle player,
                                         uint8_t* paused);
alumbra_status alumbra_player_get_ended(alumbra_handle player, uint8_t* ended);
alumbra_status alumbra_player_get_autoplay(alumbra_handle player,
                                           uint8_t* autoplay);
alumbra_status alumbra_player_set_autoplay(alumbra_handle player,
                                           uint8_t autoplay);
alumbra_status alumbra_player_get_loop(alumbra_handle player, uint8_t* loop);
alumbra_status alumbra_player_set_loop(alumbra_handle player, uint8_t loop);
alumbra_status alumbra_player_get_duration(alumbra_handle player,
                                           double* seconds);
alumbra_status alumbra_player_get_preload(alumbra_handle player,
                                          uint32_t* preload);
alumbra_status alumbra_player_set_preload(alumbra_handle player,
                                          uint32_t preload);
alumbra_status alumbra_player_get_default_playback_rate(alumbra_handle player,
                                                        double* rate);
alumbra_status alumbra_player_set_default_playback_rate(alumbra_handle player,
                                                        double rate);
alumbra_status alumbra_player_get_preserves_pitch(alumbra_handle player,
                                                  uint8_t* preserves_pitch);
alumbra_status alumbra_player_set_preserves_pitch(alumbra_handle player,
                                                  uint8_t preserves_pitch);
alumbra_status alumbra_player_get_time_ranges(alumbra_handle player,
                                              uint32_t kind, size_t* count);
alumbra_status alumbra_player_get_time_range(alumbra_handle player,
                                             uint32_t kind, size_t index,
                                             double* start, double* end);
alumbra_status alumbra_player_get_current_time(alumbra_handle player,
                                               double* seconds);
alumbra_status alumbra_player_set_current_time(alumbra_handle player,
                                               double seconds);
alumbra_status alumbra_player_get_seeking(alumbra_handle player,
                                          uint8_t* seeking);
alumbra_status alumbra_player_get_volume(alumbra_handle player, double* volume);
alumbra_status alumbra_player_set_volume(alumbra_handle player, double volume);
alumbra_status alumbra_player_get_muted(alumbra_handle player, uint8_t* muted);
alumbra_status alumbra_player_set_muted(alumbra_handle player, uint8_t muted);
alumbra_status alumbra_player_get_playback_rate(alumbra_handle player,
                                                double* rate);
alumbra_status alumbra_player_set_playback_rate(alumbra_handle player,
                                                double rate);
alumbra_status alumbra_player_get_ready_state(alumbra_handle player,
                                              uint32_t* state);
alumbra_status alumbra_player_get_network_state(alumbra_handle player,
                                                uint32_t* state);
alumbra_status alumbra_player_get_statistics(alumbra_handle player,
                                             alumbra_statistics* statistics);
alumbra_status alumbra_player_get_error(alumbra_handle player,
                                        alumbra_error_info* error,
                                        char* message, size_t* message_size);
alumbra_status alumbra_player_poll_event(alumbra_handle player,
                                         alumbra_event* event,
                                         uint8_t* available);
alumbra_status alumbra_settings_get(alumbra_handle player, const char* key,
                                    size_t key_size, char* value,
                                    size_t* value_size);
alumbra_status alumbra_settings_set(alumbra_handle player, const char* key,
                                    size_t key_size, const char* value,
                                    size_t value_size);
alumbra_status alumbra_player_text_track_count(alumbra_handle player,
                                               size_t* count);
alumbra_status alumbra_player_text_track_at(alumbra_handle player, size_t index,
                                            alumbra_track_handle* track);
alumbra_status alumbra_player_add_text_track(
    alumbra_handle player, uint32_t kind, const char* label, size_t label_size,
    const char* language, size_t language_size, alumbra_track_handle* track);
alumbra_status alumbra_text_track_release(alumbra_track_handle track);
alumbra_status alumbra_text_track_get_kind(alumbra_track_handle track,
                                           uint32_t* kind);
alumbra_status alumbra_text_track_get_mode(alumbra_track_handle track,
                                           uint32_t* mode);
alumbra_status alumbra_text_track_set_mode(alumbra_track_handle track,
                                           uint32_t mode);
alumbra_status alumbra_text_track_get_id(alumbra_track_handle track, char* id,
                                         size_t* id_size);
alumbra_status alumbra_text_track_get_label(alumbra_track_handle track,
                                            char* label, size_t* label_size);
alumbra_status alumbra_text_track_get_language(alumbra_track_handle track,
                                               char* language,
                                               size_t* language_size);

#ifdef __cplusplus
}
#endif

#endif
