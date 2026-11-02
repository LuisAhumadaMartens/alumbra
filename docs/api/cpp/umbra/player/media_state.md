\file umbra/player/media_state.h
The state enumerations of the media element: `networkState`, `readyState`, `preload` and the answer of
`canPlayType()`.

\enum umbra::player::NetworkState
The network state of the element.

HTML counterpart: the `NETWORK_*` constants of `HTMLMediaElement`, with the same numeric values.

\var umbra::player::NetworkState::kEmpty
`NETWORK_EMPTY` (0): no resource is selected and the element has not been initialised.

\var umbra::player::NetworkState::kIdle
`NETWORK_IDLE` (1): the resource is selected and nothing is being fetched now.

\var umbra::player::NetworkState::kLoading
`NETWORK_LOADING` (2): the player is fetching data.

\var umbra::player::NetworkState::kNoSource
`NETWORK_NO_SOURCE` (3): the resource selection found no usable resource.

\enum umbra::player::ReadyState
How much of the media the element has.

HTML counterpart: the `HAVE_*` constants of `HTMLMediaElement`, with the same numeric values. The level is the lowest
among the selected tracks.

\var umbra::player::ReadyState::kHaveNothing
`HAVE_NOTHING` (0): no information about the media.

\var umbra::player::ReadyState::kHaveMetadata
`HAVE_METADATA` (1): the duration and tracks are known, but there is no data at the position.

\var umbra::player::ReadyState::kHaveCurrentData
`HAVE_CURRENT_DATA` (2): data for the position is present, but not enough to advance.

\var umbra::player::ReadyState::kHaveFutureData
`HAVE_FUTURE_DATA` (3): enough data to advance for a short time.

\var umbra::player::ReadyState::kHaveEnoughData
`HAVE_ENOUGH_DATA` (4): enough data that playback can run to the end at the current rate.

\enum umbra::player::Preload
The `preload` hint.

HTML counterpart: the `none`, `metadata` and `auto` values of the `preload` attribute.

\var umbra::player::Preload::kNone
`none`: the author prefers that nothing is fetched before playback.

\var umbra::player::Preload::kMetadata
`metadata`: the author prefers that only the metadata is fetched. This is the default.

\var umbra::player::Preload::kAuto
`auto`: the author allows fetching ahead of playback.

\enum umbra::player::CanPlayTypeResult
The answer of Player::CanPlayType().

HTML counterpart: the empty string, `"maybe"` and `"probably"`.

\var umbra::player::CanPlayTypeResult::kNone
The empty string: the type is not supported.

\var umbra::player::CanPlayTypeResult::kMaybe
`"maybe"`: the container is supported but the codecs could not be confirmed.

\var umbra::player::CanPlayTypeResult::kProbably
`"probably"`: the container and every named codec are supported.
