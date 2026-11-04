# Player source routing

`umbra::player::Player::Create` assembles the default port, the source scheme
registry and the source resolver. `set_src()` selects a byte stream from the
port's registered providers. The URL resolver removes user information and
records credentials before the source reads any bytes.

The source type detector reads the beginning of the response through the same
buffered reader used by the demuxer. HLS and DASH manifests are parsed into a
presentation and passed to `AdaptiveSource`. Other supported types use
`ProgressiveSource`, which creates a demuxer from the progressive demuxer
registry.

`ProgressiveSource` pumps the demuxer asynchronously. It reads at requested
byte offsets, including tail seeks for an MP4 whose `moov` box follows its
media data. The demuxer's seek map supplies duration and seek points. Samples
are delivered to the playback state machine with their demuxed track ids, and
end of stream is reported after queued samples have been consumed.

The desktop port registers `file`, `http` and `https` byte streams and exposes
its HTTP transport to adaptive segment fetching. Headless playback uses the
same source and decode path as windowed playback, with null video and dummy
audio output.
