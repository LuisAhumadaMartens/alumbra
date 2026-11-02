\file umbra/player/play_result.h
The result type of Player::Play().

C++ has no promise, so `play()` takes a callback (ADR-0023). The result is an Alumbra-owned C++20 type: no
`absl::Status`, `std::expected` or `std::move_only_function` appears in a public signature.

\enum umbra::player::PlayResultCode
The outcome of a play request.

HTML counterpart: how the promise of `play()` settles.

\var umbra::player::PlayResultCode::kOk
The promise resolved: playback has started.

\var umbra::player::PlayResultCode::kNotAllowed
`NotAllowedError`: platform policy refuses playback, as a browser's autoplay rules do.

\var umbra::player::PlayResultCode::kNotSupported
`NotSupportedError`: the element failed with `MEDIA_ERR_SRC_NOT_SUPPORTED`. PlayResult::detail carries the cause.

\var umbra::player::PlayResultCode::kAbort
`AbortError`: `pause()` or `load()` interrupted the pending play.

\struct umbra::player::PlayResult
The code and optional detail of a finished play request.

\var umbra::player::PlayResult::code
The outcome; PlayResultCode::kOk by default.

\var umbra::player::PlayResult::detail
The detail of the error behind PlayResultCode::kNotSupported, and empty for every other code.

\typedef umbra::player::PlayCallback
Receives the result of Player::Play() exactly once, from Player::DispatchEvents().

If the player is destroyed first, the callback is destroyed without running.
