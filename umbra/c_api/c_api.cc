#include "umbra/c_api/c_api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "umbra/c_api/testing.h"
#include "umbra/player/events.h"
#include "umbra/player/media_error.h"
#include "umbra/player/play_result.h"
#include "umbra/player/player.h"
#include "umbra/settings/platform.h"
#include "umbra/settings/setting_value.h"
#include "umbra/settings/settings_keys.h"
#include "umbra/settings/store.h"

namespace {

struct ErrorCodeMap {
  umbra::player::MediaErrorCode player_code;
  uint32_t c_code;
};

constexpr std::array<ErrorCodeMap, 4> kErrorCodeMap{{
    {.player_code = umbra::player::MediaErrorCode::kAborted, .c_code = 1},
    {.player_code = umbra::player::MediaErrorCode::kNetwork, .c_code = 2},
    {.player_code = umbra::player::MediaErrorCode::kDecode, .c_code = 3},
    {.player_code = umbra::player::MediaErrorCode::kSrcNotSupported,
     .c_code = 4},
}};

uint32_t ToCErrorCode(umbra::player::MediaErrorCode code) {
  for (const ErrorCodeMap& entry : kErrorCodeMap) {
    if (entry.player_code == code) {
      return entry.c_code;
    }
  }
  return 3;
}

alumbra_status ToCStatus(const absl::Status& status) {
  switch (status.code()) {
    case absl::StatusCode::kOk:
      return ALUMBRA_STATUS_OK;
    case absl::StatusCode::kInvalidArgument:
    case absl::StatusCode::kOutOfRange:
      return ALUMBRA_STATUS_INVALID_ARGUMENT;
    case absl::StatusCode::kUnimplemented:
    default:
      return ALUMBRA_STATUS_UNSUPPORTED;
  }
}

umbra::settings::Platform CurrentPlatform() {
  return static_cast<umbra::settings::Platform>(ALUMBRA_SETTINGS_PLATFORM);
}

struct PlayerContext {
  struct PostedTask {
    std::chrono::steady_clock::time_point due;
    std::function<void()> task;
  };

  std::unique_ptr<umbra::player::Player> player;
  umbra::settings::Store settings{umbra::settings::GetRegistry(),
                                  CurrentPlatform()};
  std::vector<umbra::player::Subscription> subscriptions;
  std::mutex event_mutex;
  std::deque<alumbra_event> events;
  std::thread::id owner = std::this_thread::get_id();
  void (*notify)(void*) = nullptr;
  void* user_data = nullptr;
  uint64_t next_sequence = 1;
  std::mutex task_mutex;
  std::deque<PostedTask> tasks;
  bool pumping_tasks = false;
};

struct PlayerSlot {
  uint32_t generation = 1;
  std::shared_ptr<PlayerContext> context;
};

struct TrackSlot {
  uint32_t generation = 1;
  std::weak_ptr<PlayerContext> context;
  std::shared_ptr<umbra::player::TextTrack> track;
};

struct ApiState {
  std::mutex slots_mutex;
  std::vector<PlayerSlot> slots;
  std::mutex tracks_mutex;
  std::vector<TrackSlot> tracks;
  std::mutex creator_mutex;
  umbra::c_api::testing::PlayerCreator player_creator;
};

ApiState& GetApiState() {
  static ApiState state;
  return state;
}

alumbra_handle MakeHandle(std::size_t index, uint32_t generation) {
  return (static_cast<uint64_t>(generation) << 32U) |
         static_cast<uint64_t>(index + 1U);
}

std::shared_ptr<PlayerContext> FindPlayer(alumbra_handle handle) {
  if (handle == 0) {
    return nullptr;
  }
  const auto slot_id = static_cast<uint32_t>(handle);
  const auto generation = static_cast<uint32_t>(handle >> 32U);
  if (slot_id == 0 || generation == 0) {
    return nullptr;
  }
  const std::scoped_lock lock(GetApiState().slots_mutex);
  const auto index = static_cast<std::size_t>(slot_id - 1U);
  if (index >= GetApiState().slots.size() ||
      GetApiState().slots[index].generation != generation) {
    return nullptr;
  }
  return GetApiState().slots[index].context;
}

struct TrackReference {
  std::shared_ptr<PlayerContext> context;
  std::shared_ptr<umbra::player::TextTrack> track;
};

alumbra_track_handle StoreTrack(
    const std::shared_ptr<PlayerContext>& context,
    std::shared_ptr<umbra::player::TextTrack> track) {
  const std::scoped_lock lock(GetApiState().tracks_mutex);
  for (std::size_t index = 0; index < GetApiState().tracks.size(); ++index) {
    TrackSlot& slot = GetApiState().tracks[index];
    if (slot.track == nullptr) {
      slot.context = context;
      slot.track = std::move(track);
      return MakeHandle(index, slot.generation);
    }
  }
  GetApiState().tracks.push_back(
      {.generation = 1, .context = context, .track = std::move(track)});
  return MakeHandle(GetApiState().tracks.size() - 1U, 1);
}

TrackReference FindTrack(alumbra_track_handle handle) {
  if (handle == 0) {
    return {};
  }
  const auto slot_id = static_cast<uint32_t>(handle);
  const auto generation = static_cast<uint32_t>(handle >> 32U);
  if (slot_id == 0 || generation == 0) {
    return {};
  }
  const std::scoped_lock lock(GetApiState().tracks_mutex);
  const auto index = static_cast<std::size_t>(slot_id - 1U);
  if (index >= GetApiState().tracks.size()) {
    return {};
  }
  const TrackSlot& slot = GetApiState().tracks[index];
  if (slot.generation != generation || slot.track == nullptr) {
    return {};
  }
  return {.context = slot.context.lock(), .track = slot.track};
}

alumbra_status CheckOwner(const std::shared_ptr<PlayerContext>& context) {
  return context->owner == std::this_thread::get_id()
             ? ALUMBRA_STATUS_OK
             : ALUMBRA_STATUS_WRONG_THREAD;
}

void PumpTasks(const std::shared_ptr<PlayerContext>& context) {
  if (context->pumping_tasks) {
    return;
  }
  context->pumping_tasks = true;
  for (;;) {
    PlayerContext::PostedTask entry;
    {
      const std::scoped_lock lock(context->task_mutex);
      if (context->tasks.empty() ||
          context->tasks.front().due > std::chrono::steady_clock::now()) {
        break;
      }
      entry = std::move(context->tasks.front());
      context->tasks.pop_front();
    }
    entry.task();
  }
  context->pumping_tasks = false;
}

template <typename Event>
void PushEvent(const std::weak_ptr<PlayerContext>& weak, const Event& event) {
  const std::shared_ptr<PlayerContext> context = weak.lock();
  if (context == nullptr) {
    return;
  }
  alumbra_event output{};
  output.struct_size = sizeof(alumbra_event);
  output.type = static_cast<uint32_t>(Event::kType);
  output.current_time = event.state.current_time;
  output.duration = event.state.duration;
  if constexpr (Event::kType == umbra::player::EventType::kError) {
    output.error_code = ToCErrorCode(event.error.code());
  } else if constexpr (Event::kType == umbra::player::EventType::kWarning) {
    output.error_code =
        ToCErrorCode(umbra::player::MediaErrorCodeFor(event.detail));
  }
  {
    const std::scoped_lock lock(context->event_mutex);
    output.sequence = context->next_sequence++;
    context->events.push_back(output);
  }
}

template <typename Event>
void AddSubscription(const std::shared_ptr<PlayerContext>& context) {
  context->subscriptions.push_back(context->player->AddEventListener<Event>(
      [weak = std::weak_ptr<PlayerContext>(context)](const Event& event) {
        PushEvent(weak, event);
      }));
}

void SubscribeAll(const std::shared_ptr<PlayerContext>& context) {
  AddSubscription<umbra::player::LoadStartEvent>(context);
  AddSubscription<umbra::player::ProgressEvent>(context);
  AddSubscription<umbra::player::SuspendEvent>(context);
  AddSubscription<umbra::player::AbortEvent>(context);
  AddSubscription<umbra::player::ErrorEvent>(context);
  AddSubscription<umbra::player::EmptiedEvent>(context);
  AddSubscription<umbra::player::StalledEvent>(context);
  AddSubscription<umbra::player::LoadedMetadataEvent>(context);
  AddSubscription<umbra::player::LoadedDataEvent>(context);
  AddSubscription<umbra::player::CanPlayEvent>(context);
  AddSubscription<umbra::player::CanPlayThroughEvent>(context);
  AddSubscription<umbra::player::PlayingEvent>(context);
  AddSubscription<umbra::player::WaitingEvent>(context);
  AddSubscription<umbra::player::SeekingEvent>(context);
  AddSubscription<umbra::player::SeekedEvent>(context);
  AddSubscription<umbra::player::EndedEvent>(context);
  AddSubscription<umbra::player::DurationChangeEvent>(context);
  AddSubscription<umbra::player::TimeUpdateEvent>(context);
  AddSubscription<umbra::player::PlayEvent>(context);
  AddSubscription<umbra::player::PauseEvent>(context);
  AddSubscription<umbra::player::RateChangeEvent>(context);
  AddSubscription<umbra::player::ResizeEvent>(context);
  AddSubscription<umbra::player::VolumeChangeEvent>(context);
  AddSubscription<umbra::player::WarningEvent>(context);
}

alumbra_status ValidateBytes(const char* bytes, size_t size) {
  return bytes != nullptr || size == 0 ? ALUMBRA_STATUS_OK
                                       : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status CopyString(const std::string& text, char* output,
                          size_t* output_size) {
  if (output_size == nullptr || ValidateBytes(output, *output_size) != 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  const std::size_t capacity = *output_size;
  *output_size = text.size();
  if (output == nullptr || capacity < text.size()) {
    return text.empty() ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_BUFFER_TOO_SMALL;
  }
  std::ranges::copy(text, output);
  return ALUMBRA_STATUS_OK;
}

}

namespace umbra::c_api::testing {

void SetPlayerCreator(PlayerCreator creator) {
  const std::scoped_lock lock(GetApiState().creator_mutex);
  GetApiState().player_creator = std::move(creator);
}

}

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
  auto context = std::make_shared<PlayerContext>();
  context->notify = config->notify;
  context->user_data = config->user_data;
  umbra::player::PlayerOptions options;
  options.post_task = [weak = std::weak_ptr<PlayerContext>(context)](
                          std::function<void()> task,
                          std::chrono::microseconds delay) {
    if (const auto locked = weak.lock(); locked != nullptr) {
      const std::scoped_lock lock(locked->task_mutex);
      const auto due = std::chrono::steady_clock::now() + delay;
      const auto position =
          std::upper_bound(locked->tasks.begin(), locked->tasks.end(), due,
                           [](std::chrono::steady_clock::time_point time,
                              const PlayerContext::PostedTask& entry) {
                             return time < entry.due;
                           });
      locked->tasks.insert(position, {.due = due, .task = std::move(task)});
    }
  };
  options.runs_on_sequence = [owner = context->owner] {
    return owner == std::this_thread::get_id();
  };
  options.on_events_available = [weak = std::weak_ptr<PlayerContext>(context)] {
    if (const auto locked = weak.lock();
        locked != nullptr && locked->notify != nullptr) {
      locked->notify(locked->user_data);
    }
  };
  umbra::c_api::testing::PlayerCreator creator;
  {
    const std::scoped_lock lock(GetApiState().creator_mutex);
    creator = GetApiState().player_creator;
  }
  context->player = creator ? creator(std::move(options))
                            : umbra::player::Player::Create(std::move(options));
  if (context->player == nullptr) {
    return ALUMBRA_STATUS_UNSUPPORTED;
  }
  SubscribeAll(context);
  const std::scoped_lock lock(GetApiState().slots_mutex);
  for (std::size_t index = 0; index < GetApiState().slots.size(); ++index) {
    if (GetApiState().slots[index].context == nullptr) {
      GetApiState().slots[index].context = std::move(context);
      *player = MakeHandle(index, GetApiState().slots[index].generation);
      return ALUMBRA_STATUS_OK;
    }
  }
  GetApiState().slots.push_back(
      {.generation = 1, .context = std::move(context)});
  *player = MakeHandle(GetApiState().slots.size() - 1U, 1);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_destroy(alumbra_handle player) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const auto slot_id = static_cast<uint32_t>(player);
  const std::scoped_lock lock(GetApiState().slots_mutex);
  PlayerSlot& slot =
      GetApiState().slots[static_cast<std::size_t>(slot_id - 1U)];
  if (slot.context != context) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  {
    const std::scoped_lock tracks_lock(GetApiState().tracks_mutex);
    for (TrackSlot& track : GetApiState().tracks) {
      if (track.context.lock() == context) {
        track.track.reset();
        track.context.reset();
        ++track.generation;
        if (track.generation == 0) {
          track.generation = 1;
        }
      }
    }
  }
  slot.context.reset();
  ++slot.generation;
  if (slot.generation == 0) {
    slot.generation = 1;
  }
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_source(alumbra_handle player, char* source,
                                         size_t* source_size) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (source_size == nullptr || ValidateBytes(source, *source_size) != 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const std::string& text = context->player->src();
  const std::size_t capacity = *source_size;
  *source_size = text.size();
  if (source == nullptr || capacity < text.size()) {
    return text.empty() ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_BUFFER_TOO_SMALL;
  }
  std::ranges::copy(text, source);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_source(alumbra_handle player,
                                         const char* source,
                                         size_t source_size) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (source == nullptr || source_size == 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  context->player->set_src(std::string(source, source_size));
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_load(alumbra_handle player) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  context->player->Load();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_play(alumbra_handle player) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  context->player->Play([](const umbra::player::PlayResult&) {});
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_pause(alumbra_handle player) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  context->player->Pause();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_fast_seek(alumbra_handle player, double seconds) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (!std::isfinite(seconds)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const bool accepted = context->player->FastSeek(seconds);
  PumpTasks(context);
  return accepted ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status alumbra_player_can_play_type(alumbra_handle player,
                                            const char* type, size_t type_size,
                                            uint32_t* result) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (type == nullptr || result == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *result = static_cast<uint32_t>(
      context->player->CanPlayType(std::string_view(type, type_size)));
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_paused(alumbra_handle player,
                                         uint8_t* paused) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (paused == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *paused = context->player->paused() ? 1 : 0;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_ended(alumbra_handle player, uint8_t* ended) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (ended == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *ended = context->player->ended() ? 1 : 0;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_autoplay(alumbra_handle player,
                                           uint8_t* autoplay) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (autoplay == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *autoplay = context->player->autoplay() ? 1 : 0;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_autoplay(alumbra_handle player,
                                           uint8_t autoplay) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (autoplay > 1) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  context->player->set_autoplay(autoplay != 0);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_loop(alumbra_handle player, uint8_t* loop) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (loop == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *loop = context->player->loop() ? 1 : 0;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_loop(alumbra_handle player, uint8_t loop) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (loop > 1) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  context->player->set_loop(loop != 0);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_duration(alumbra_handle player,
                                           double* seconds) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (seconds == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *seconds = context->player->duration();
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_preload(alumbra_handle player,
                                          uint32_t* preload) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (preload == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *preload = static_cast<uint32_t>(context->player->preload());
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_preload(alumbra_handle player,
                                          uint32_t preload) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (preload > ALUMBRA_PRELOAD_AUTO) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  context->player->set_preload(static_cast<umbra::player::Preload>(preload));
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_default_playback_rate(alumbra_handle player,
                                                        double* rate) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (rate == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *rate = context->player->default_playback_rate();
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_default_playback_rate(alumbra_handle player,
                                                        double rate) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (!std::isfinite(rate)) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  return context->player->set_default_playback_rate(rate)
             ? ALUMBRA_STATUS_OK
             : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status alumbra_player_get_preserves_pitch(alumbra_handle player,
                                                  uint8_t* preserves_pitch) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (preserves_pitch == nullptr) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  *preserves_pitch = context->player->preserves_pitch() ? 1 : 0;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_preserves_pitch(alumbra_handle player,
                                                  uint8_t preserves_pitch) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (preserves_pitch > 1) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  context->player->set_preserves_pitch(preserves_pitch != 0);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_time_ranges(alumbra_handle player,
                                              uint32_t kind, size_t* count) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (kind > ALUMBRA_TIME_RANGES_SEEKABLE || count == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  const auto ranges = kind == ALUMBRA_TIME_RANGES_BUFFERED
                          ? context->player->buffered()
                          : context->player->seekable();
  *count = ranges.length();
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_time_range(alumbra_handle player,
                                             uint32_t kind, size_t index,
                                             double* start, double* end) {
  const auto context = FindPlayer(player);
  if (context == nullptr) return ALUMBRA_STATUS_INVALID_HANDLE;
  if (kind > ALUMBRA_TIME_RANGES_SEEKABLE || start == nullptr ||
      end == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0)
    return status;
  const auto ranges = kind == ALUMBRA_TIME_RANGES_BUFFERED
                          ? context->player->buffered()
                          : context->player->seekable();
  if (index >= ranges.length()) return ALUMBRA_STATUS_INVALID_ARGUMENT;
  *start = ranges.start(index);
  *end = ranges.end(index);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_current_time(alumbra_handle player,
                                               double* seconds) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (seconds == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *seconds = context->player->current_time();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_current_time(alumbra_handle player,
                                               double seconds) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (!std::isfinite(seconds)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const bool accepted = context->player->set_current_time(seconds);
  PumpTasks(context);
  return accepted ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status alumbra_player_get_seeking(alumbra_handle player,
                                          uint8_t* seeking) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (seeking == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *seeking = context->player->seeking() ? 1 : 0;
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_volume(alumbra_handle player,
                                         double* volume) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (volume == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *volume = context->player->volume();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_volume(alumbra_handle player, double volume) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (!std::isfinite(volume)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const bool changed = context->player->set_volume(volume);
  PumpTasks(context);
  return changed ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status alumbra_player_get_muted(alumbra_handle player, uint8_t* muted) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (muted == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *muted = context->player->muted() ? 1 : 0;
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_muted(alumbra_handle player, uint8_t muted) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (muted > 1) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  context->player->set_muted(muted != 0);
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_playback_rate(alumbra_handle player,
                                                double* rate) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (rate == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *rate = context->player->playback_rate();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_set_playback_rate(alumbra_handle player,
                                                double rate) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (!std::isfinite(rate)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const bool changed = context->player->set_playback_rate(rate);
  PumpTasks(context);
  return changed ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_INVALID_ARGUMENT;
}

alumbra_status alumbra_player_get_ready_state(alumbra_handle player,
                                              uint32_t* state) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (state == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *state = static_cast<uint32_t>(context->player->ready_state());
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_network_state(alumbra_handle player,
                                                uint32_t* state) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (state == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *state = static_cast<uint32_t>(context->player->network_state());
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_statistics(alumbra_handle player,
                                             alumbra_statistics* statistics) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (statistics == nullptr || statistics->struct_size < sizeof(*statistics)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const auto snapshot = context->player->Statistics();
  alumbra_statistics result{};
  result.struct_size = sizeof(alumbra_statistics);
  const auto set_double = [&result](uint32_t index, const auto& value,
                                    double& target) {
    if (value.has_value()) {
      result.present_mask |= uint64_t{1} << index;
      target = *value;
    }
  };
  const auto set_integer = [&result](uint32_t index, const auto& value,
                                     uint64_t& target) {
    if (value.has_value()) {
      result.present_mask |= uint64_t{1} << index;
      target = *value;
    }
  };
  set_double(ALUMBRA_STATISTIC_AVERAGE_INITIAL_STARTUP_TIME,
             snapshot.average_initial_startup_time_s.value,
             result.average_initial_startup_time_s);
  set_integer(ALUMBRA_STATISTIC_STARTUP_FAILURES,
              snapshot.startup_failures.value, result.startup_failures);
  set_double(ALUMBRA_STATISTIC_PLAYBACK_FAILURE_PERCENTAGE,
             snapshot.playback_failure_percentage.value,
             result.playback_failure_percentage);
  set_double(ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_STALLED_COUNT,
             snapshot.average_playback_stalled_count.value,
             result.average_playback_stalled_count);
  set_double(ALUMBRA_STATISTIC_AVERAGE_STALLED_TIME_PERCENTAGE,
             snapshot.average_stalled_time_percentage.value,
             result.average_stalled_time_percentage);
  set_double(ALUMBRA_STATISTIC_AVERAGE_PLAYBACK_BITRATE,
             snapshot.average_playback_bitrate_kbps.value,
             result.average_playback_bitrate_kbps);
  set_double(ALUMBRA_STATISTIC_AVERAGE_SEGMENT_BITRATE,
             snapshot.average_segment_bitrate_bps.value,
             result.average_segment_bitrate_bps);
  set_integer(ALUMBRA_STATISTIC_RENDITION_SWITCH_COUNT,
              snapshot.rendition_switch_count.value,
              result.rendition_switch_count);
  set_integer(ALUMBRA_STATISTIC_DROPPED_FRAMES, snapshot.dropped_frames.value,
              result.dropped_frames);
  set_double(ALUMBRA_STATISTIC_LIVE_LATENCY, snapshot.live_latency_ms.value,
             result.live_latency_ms);
  *statistics = result;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_get_error(alumbra_handle player,
                                        alumbra_error_info* error,
                                        char* message, size_t* message_size) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (error == nullptr || error->struct_size < sizeof(alumbra_error_info) ||
      message_size == nullptr || ValidateBytes(message, *message_size) != 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const std::size_t capacity = *message_size;
  PumpTasks(context);
  const auto current = context->player->error();
  if (!current.has_value()) {
    error->code = 0;
    error->message_size = 0;
    *message_size = 0;
    return ALUMBRA_STATUS_OK;
  }
  const std::string& text = current->message();
  error->code = ToCErrorCode(current->code());
  error->message_size = static_cast<uint32_t>(text.size());
  *message_size = text.size();
  if (message == nullptr || capacity < text.size()) {
    return text.empty() ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_BUFFER_TOO_SMALL;
  }
  std::ranges::copy(text, message);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_poll_event(alumbra_handle player,
                                         alumbra_event* event,
                                         uint8_t* available) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (event == nullptr || available == nullptr ||
      event->struct_size <
          offsetof(alumbra_event, error_code) + sizeof(event->error_code)) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  PumpTasks(context);
  context->player->DispatchEvents();
  const std::scoped_lock lock(context->event_mutex);
  if (context->events.empty()) {
    *available = 0;
    return ALUMBRA_STATUS_OK;
  }
  const uint32_t struct_size = event->struct_size;
  const alumbra_event output = context->events.front();
  std::memcpy(event, &output,
              std::min<std::size_t>(struct_size, sizeof(output)));
  event->struct_size = struct_size;
  context->events.pop_front();
  *available = 1;
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_settings_get(alumbra_handle player, const char* key,
                                    size_t key_size, char* value,
                                    size_t* value_size) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (key == nullptr || key_size == 0 || value_size == nullptr ||
      ValidateBytes(value, *value_size) != 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const std::string_view key_view(key, key_size);
  const auto found =
      umbra::settings::FindSetting(context->settings.Schema(), key_view);
  if (!found.has_value()) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  const std::string text = umbra::settings::FormatValue(
      context->settings.TakeSnapshot().GetValue(found->index));
  const std::size_t capacity = *value_size;
  *value_size = text.size();
  if (value == nullptr || capacity < text.size()) {
    return text.empty() ? ALUMBRA_STATUS_OK : ALUMBRA_STATUS_BUFFER_TOO_SMALL;
  }
  std::ranges::copy(text, value);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_settings_set(alumbra_handle player, const char* key,
                                    size_t key_size, const char* value,
                                    size_t value_size) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (key == nullptr || key_size == 0 ||
      ValidateBytes(value, value_size) != 0) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const alumbra_status status = ToCStatus(context->settings.SetFromString(
      umbra::settings::Layer::kUser, std::string_view(key, key_size),
      std::string_view(value == nullptr ? "" : value, value_size)));
  PumpTasks(context);
  return status;
}

alumbra_status alumbra_player_text_track_count(alumbra_handle player,
                                               size_t* count) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (count == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  *count = context->player->text_tracks().length();
  PumpTasks(context);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_text_track_at(alumbra_handle player, size_t index,
                                            alumbra_track_handle* track) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (track == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  const auto value = context->player->text_tracks().item(index);
  if (value == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  *track = StoreTrack(context, value);
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_player_add_text_track(
    alumbra_handle player, uint32_t kind, const char* label, size_t label_size,
    const char* language, size_t language_size, alumbra_track_handle* track) {
  const auto context = FindPlayer(player);
  if (context == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (kind > ALUMBRA_TEXT_TRACK_METADATA || track == nullptr ||
      label == nullptr || language == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(context); status != 0) {
    return status;
  }
  auto value = context->player->AddTextTrack(
      static_cast<umbra::player::TextTrackKind>(kind),
      std::string(label, label_size), std::string(language, language_size));
  if (value == nullptr) {
    return ALUMBRA_STATUS_UNSUPPORTED;
  }
  *track = StoreTrack(context, std::move(value));
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_text_track_release(alumbra_track_handle track) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  const auto slot_id = static_cast<uint32_t>(track);
  const std::scoped_lock lock(GetApiState().tracks_mutex);
  TrackSlot& slot =
      GetApiState().tracks[static_cast<std::size_t>(slot_id - 1U)];
  if (slot.generation != static_cast<uint32_t>(track >> 32U) ||
      slot.track != reference.track) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  slot.track.reset();
  slot.context.reset();
  ++slot.generation;
  if (slot.generation == 0) {
    slot.generation = 1;
  }
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_text_track_get_kind(alumbra_track_handle track,
                                           uint32_t* kind) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (kind == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  *kind = static_cast<uint32_t>(reference.track->kind());
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_text_track_get_mode(alumbra_track_handle track,
                                           uint32_t* mode) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (mode == nullptr) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  *mode = static_cast<uint32_t>(reference.track->mode());
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_text_track_set_mode(alumbra_track_handle track,
                                           uint32_t mode) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (mode > ALUMBRA_TEXT_TRACK_SHOWING) {
    return ALUMBRA_STATUS_INVALID_ARGUMENT;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  reference.track->set_mode(static_cast<umbra::player::TextTrackMode>(mode));
  return ALUMBRA_STATUS_OK;
}

alumbra_status alumbra_text_track_get_id(alumbra_track_handle track, char* id,
                                         size_t* id_size) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  return CopyString(reference.track->id(), id, id_size);
}

alumbra_status alumbra_text_track_get_label(alumbra_track_handle track,
                                            char* label, size_t* label_size) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  return CopyString(reference.track->label(), label, label_size);
}

alumbra_status alumbra_text_track_get_language(alumbra_track_handle track,
                                               char* language,
                                               size_t* language_size) {
  const auto reference = FindTrack(track);
  if (reference.context == nullptr || reference.track == nullptr) {
    return ALUMBRA_STATUS_INVALID_HANDLE;
  }
  if (const alumbra_status status = CheckOwner(reference.context);
      status != 0) {
    return status;
  }
  return CopyString(reference.track->language(), language, language_size);
}
}
