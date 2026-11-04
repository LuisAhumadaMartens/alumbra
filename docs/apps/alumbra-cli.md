# alumbra-cli

`alumbra-cli` is the desktop reference command-line player. It accepts one URL or local path and uses the public C++ player API.

```sh
alumbra-cli movie.mkv
alumbra-cli https://example.test/video.mp4
alumbra-cli --headless --start 30 movie.mp4
alumbra-cli --list-settings
```

Windowed playback is the default on desktop builds with the UI package enabled. `--headless` selects null video output and dummy audio output. Use `--help` to list all options. `--set key=value` can be repeated; each value is validated against the generated settings schema and applied to the player's integrator layer. Settings wired into the current playback configuration include `player.events.timeupdate_interval_ms`, `buffer.start_threshold_ms`, `buffer.rebuffer_threshold_ms`, `buffer.video_max_bytes` and `buffer.audio_max_bytes`.

The window uses the Antumbra desktop controls. `Space`, `k` and `MediaPlayPause` toggle playback; `j` and `l` seek back and forward; the arrow keys seek or adjust volume; `m` mutes; `f` toggles fullscreen; `Tab` and `Shift+Tab` move focus; `Enter` activates a focused control; and `Escape` backs out of a menu or hides the controls.

`--log-level` sets this player's runtime log threshold. `--trace` takes a comma-separated list of categories, such as `player,buffer`, and writes matching trace records to stderr as they occur.

Playback errors are written to stderr. Usage errors return 2; playback errors return 11 through 14, matching the four `MediaError` codes. Normal end of stream returns 0.

Builds without the desktop UI package retain headless playback and argument inspection; they reject windowed playback.
