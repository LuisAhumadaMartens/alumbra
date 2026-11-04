\file umbra/player/tracks.h
Typed descriptions of the audio tracks and video renditions in a player's current source.

The collections are snapshots returned by `Player::audio_tracks()` and `Player::video_tracks()`. Track identifiers
are source-specific and remain stable while the same manifest track remains available. `AudioTrack::enabled` and
`VideoTrack::selected` report the current selection.

\enum umbra::player::AudioTrackKind
The role of an audio track.

\var umbra::player::AudioTrackKind::kMain
Main programme audio.

\var umbra::player::AudioTrackKind::kAlternative
Alternative audio.

\var umbra::player::AudioTrackKind::kDescriptions
Audio description.

\var umbra::player::AudioTrackKind::kCommentary
Commentary audio.

\var umbra::player::AudioTrackKind::kDub
Dubbed audio.

\var umbra::player::AudioTrackKind::kSign
Sign-language audio.

\var umbra::player::AudioTrackKind::kEmergency
Emergency audio.

\var umbra::player::AudioTrackKind::kCaptions
Caption audio.

\var umbra::player::AudioTrackKind::kSubtitles
Subtitle audio.

\var umbra::player::AudioTrackKind::kMetadata
Metadata audio.

\enum umbra::player::VideoTrackKind
The role of a video track.

\var umbra::player::VideoTrackKind::kMain
Main programme video.

\var umbra::player::VideoTrackKind::kAlternative
Alternative video.

\var umbra::player::VideoTrackKind::kDescriptions
Video associated with an audio-description presentation.

\var umbra::player::VideoTrackKind::kCommentary
Commentary video.

\var umbra::player::VideoTrackKind::kEmergency
Emergency video.

\var umbra::player::VideoTrackKind::kCaptions
Caption video.

\var umbra::player::VideoTrackKind::kSubtitles
Subtitle video.

\var umbra::player::VideoTrackKind::kSign
Sign-language video.

\var umbra::player::VideoTrackKind::kMetadata
Metadata video.

\struct umbra::player::AudioTrack
Describes one audio track and whether it is enabled.

\var umbra::player::AudioTrack::id
Source-specific identifier accepted by `Player::SelectAudioTrack()`.

\var umbra::player::AudioTrack::kind
Typed role from the source manifest.

\var umbra::player::AudioTrack::label
Human-readable label from the source manifest.

\var umbra::player::AudioTrack::language
BCP 47 language tag, or empty when the source does not provide one.

\var umbra::player::AudioTrack::enabled
Whether this track is selected for playback.

\struct umbra::player::VideoTrack
Describes a selectable video rendition.

\var umbra::player::VideoTrack::id
Source-specific identifier accepted by `Player::SelectVideoTrack()`.

\var umbra::player::VideoTrack::kind
Typed role from the source manifest.

\var umbra::player::VideoTrack::label
Human-readable label from the source manifest.

\var umbra::player::VideoTrack::language
BCP 47 language tag, or empty when the source does not provide one.

\var umbra::player::VideoTrack::selected
Whether this rendition is selected for playback.

\var umbra::player::VideoTrack::height
Nominal coded height in pixels, or zero when it is unknown.

\var umbra::player::VideoTrack::bitrate_bps
Nominal bandwidth in bits per second, or zero when it is unknown.

\var umbra::player::VideoTrack::codec
RFC 6381 codec string, or empty when it is unknown.
