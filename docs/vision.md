# Vision

## Why Alumbra exists

Most players that run everywhere are really *several* players: a JavaScript player on the web, ExoPlayer/Media3
on Android, AVPlayer on Apple platforms, each with its own bugs, features and settings. YouTube's living-room
stack takes a different approach: a single C++ runtime (Cobalt) implements the HTML5 media contract on top of a
thin porting layer (Starboard), so the same application logic runs on thousands of very different devices.

Alumbra applies that idea to a standalone player:

1. **One engine.** Streaming, adaptive bitrate, demuxing, A/V synchronisation, subtitles, ads and settings are
   written once, in portable C++ (**Umbra**): C++23 inside, C++20 public headers.
2. **One small porting contract.** Each platform only implements what is genuinely platform-specific — decoders,
   renderers, audio output, networking, DRM, ad SDK bridges (**Penumbra**).
3. **One familiar API.** Integrators use an `HTMLMediaElement`-shaped API everywhere — `play()`, `pause()`,
   `currentTime`, `readyState`, `textTracks`, events — so web developers already know it (**Antumbra**).
4. **Play anything.** Like VLC, any source you can type should play: files, HTTP, HLS, DASH, low-latency
   variants, `rtmp`, `rtsp`, `srt`, `udp`.

It is also a learning project, written in the open: the reasoning behind each decision is part of the deliverable.

## Goals

- **Correctness first**, then performance: conformance with HLS (RFC 8216 and its successor draft), DASH
  (ISO/IEC 23009-1), CMAF, WebVTT, and the HTML media and MSE/EME specifications where we mirror them.
- **Performance budgets** that matter on living-room hardware: time to first frame, steady-state memory on
  1 GB-class devices, CPU, dropped frames, rebuffering.
- **Compatibility** made explicit in a published [compatibility matrix](compatibility.md), backed by tests.
- **Monetisation done right:** Google IMA integration (client-side, DAI and SGAI) with ad overlays that never
  mask, or get masked by, the player UI, and that stay viewable for Open Measurement.
- **Styled, consistent UI** on every platform, including fullscreen during ad breaks, from a single design
  source of truth.
- **Every option in one place:** a single settings schema drives code, validation, defaults and documentation.
- **Easy to use:** one-line embed on the web, one-dependency install on mobile, `alumbra <url>` on desktop.
- **Documented like a product:** guides, API reference, ADRs, research notes.

## Non-goals

- **Not a platform.** No video hosting, CMS, transcoding, analytics backend, user accounts or licence server.
- **Not a browser.** Alumbra does not implement HTML, CSS or JavaScript; it implements the *media* contract.
- **Not an encoder or editor.** Alumbra plays media; it does not produce it.
- **No GPL dependencies.** Alumbra stays Apache-2.0-compatible (see [ADR-0010](adr/0010-license-and-visibility.md)).
- **No bypassing platform rules.** DRM goes through each platform's official CDM; ads follow IMA policies.

## Who it is for

- **Integrators** who want one player with one API and one settings model on every screen.
- **Engineers** who want to read a clean, documented reference of how a modern media pipeline works,
  from manifest parsing to the presented frame.

## What "done" looks like (1.0)

A user can paste any supported URL into the web demo, the desktop CLI, the Android/Google TV app or the
iOS/tvOS app; get the same playback behaviour, subtitles, styling and settings; watch IMA ads (including SGAI
breaks) in a styled fullscreen player; and read exactly why every part works the way it does.

