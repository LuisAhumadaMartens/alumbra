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

uint32_t alumbra_c_api_version(void);
alumbra_status alumbra_negotiate_version(alumbra_version_info* info);
alumbra_status alumbra_player_create(const alumbra_player_config* config,
                                     alumbra_handle* player);
alumbra_status alumbra_player_destroy(alumbra_handle player);
alumbra_status alumbra_player_set_source(alumbra_handle player,
                                         const char* source,
                                         size_t source_size);
alumbra_status alumbra_player_load(alumbra_handle player);
alumbra_status alumbra_player_play(alumbra_handle player);
alumbra_status alumbra_player_pause(alumbra_handle player);
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
