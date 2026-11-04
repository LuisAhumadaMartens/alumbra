# alumbra-cli

`alumbra-cli` is the desktop reference command-line player. It accepts one URL or local path and uses the public C++ player API.

```sh
alumbra-cli --headless movie.mkv
alumbra-cli --headless https://example.test/video.mp4
alumbra-cli --headless --start 30 movie.mp4
alumbra-cli --list-settings
```

Use `--help` to list all options. `--set key=value` can be repeated. The CLI checks each setting name and value type against the generated settings metadata. Applying settings will be available when the public player API exposes its integrator settings layer.

`--headless` selects null video output and dummy audio output. Playback errors are written to stderr. Usage errors return 2; playback errors return 11 through 14, matching the four `MediaError` codes. Normal end of stream returns 0.

The windowed renderer, interactive controls and trace output are planned with the desktop app loop. The current CLI build supports headless playback and argument inspection.
