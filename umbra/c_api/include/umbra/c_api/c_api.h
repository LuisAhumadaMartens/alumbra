#ifndef ALUMBRA_C_API_H_
#define ALUMBRA_C_API_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALUMBRA_C_API_VERSION 1u

typedef uint64_t alumbra_handle;
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
} alumbra_event;

typedef struct alumbra_error_info {
  uint32_t struct_size;
  uint32_t code;
  uint32_t message_size;
} alumbra_error_info;

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

#ifdef __cplusplus
}
#endif

#endif
