# Cobalt and Starboard

Last verified: 2026-10-31

Cobalt is Google's web runtime for TVs and other resource-constrained devices. It runs the YouTube living-room
application on top of Starboard, a C porting layer that device makers implement. This note covers how the two fit
together, where the release lines stand today, and how media flows through `SbPlayer`. It closes with the lessons
that shape Penumbra, Alumbra's porting layer.

## 1. What Cobalt is

- Google's overview describes Cobalt as a "lightweight, fully-capable HTML5/CSS/JS web platform powered by Chromium
  and Blink" for resource-constrained devices
  ([Cobalt overview](https://developers.google.com/youtube/cobalt/docs/overview)).
- It is single-process, does not require a JIT, and updates itself through Evergreen.
- The docs address two audiences:
  - *porters*, who implement Starboard for a device;
  - *developers*, who write web apps that run on Cobalt.
- The [Cobalt landing page](https://developers.google.com/youtube/cobalt) still describes it as implementing "a subset
  of HTML5/CSS/JS", "including the YouTube TV app". That wording reflects the classic engine.
- **YTLR** is common industry shorthand for YouTube's living-room (TV) application. Google's public Cobalt docs say
  "YouTube TV app" instead of using the acronym (unverified as an official Google term).
- The source lives at [github.com/youtube/cobalt](https://github.com/youtube/cobalt):
  - `main` is now a Chromium tree.
  - Release branches include `25.lts.1+`, `26.eap`, `26.android` and `27.lts`.

## 2. Classic Cobalt vs Chromium-based Cobalt

| Aspect | Classic Cobalt (up to 25.lts) | Chromium-based Cobalt (26.eap, 27.lts) |
|--------|-------------------------------|----------------------------------------|
| Web engine | Custom web runtime implementing a subset of HTML5/CSS/JS | Chromium fork with Blink and V8 (27 LTS is based on M138) |
| Media glue | `cobalt/media/base/sbplayer_pipeline.cc`, `sbplayer_bridge.cc` ([25.lts tree](https://github.com/youtube/cobalt/tree/25.lts.1%2B/cobalt/media/base)) | `media::StarboardRenderer` with `SbPlayerBridge` ([starboard_renderer.h](https://github.com/youtube/cobalt/blob/main/media/starboard/starboard_renderer.h)) |
| Starboard API | 14–16 on 25.lts | 17 on 26.eap; 18 on 27.lts and `main` |
| Status | Long-term support line | 27 LTS is the recommended line |

Supporting facts:

- **Cobalt 27 LTS.** The [27.LTS.10 release notes](https://developers.google.com/youtube/cobalt/docs/releases/27.LTS.10)
  call this "the most significant architectural evolution in Cobalt's history". They describe it as "transitioning
  the core engine from a custom web runtime to a fully-fledged Chromium fork based on M138".
  - The stable tag is `27.lts.10` (`27.lts.10.1041180`), shipped with Starboard API 18.
  - It adds WebGL 1 and 2, current V8 and standard web APIs.
  - Reference ports: Desktop Linux, RDK (`starboard/contrib/rdk`) and Android (AOSP).
  - Two Starboard extensions are newly required: `CobaltExtensionCrashHandlerApi` and
    `StarboardExtensionNativeStabilityApi`.
- **Cobalt 26 was an Early Access Release only.** Its
  [release page](https://developers.google.com/youtube/cobalt/docs/releases/26.EAP) states: "Cobalt 26 is an Early
  Access Release (e.g., 26.eap). Please use Cobalt 27 LTS and Starboard 18 instead."
  - Writing "Cobalt 26+" for the Chromium-based line is accurate, but 27 LTS is the production target.
- **"Chrobalt" is informal.** Collabora uses the nickname in
  [a September 11, 2026 post](https://www.collabora.com/news-and-blog/news-and-events/re-engineering-youtube-for-the-living-room-bringing-google-chrobalt-to-the-rdk.html)
  about an RDK reference platform. Google's release notes say "Cobalt 27" rather than "Chrobalt".
- **Migration.** Moving from 25 to 27 is documented in
  [starboard/doc/eap/25_to_27.md](https://github.com/youtube/cobalt/blob/27.lts/starboard/doc/eap/25_to_27.md).

## 3. Starboard: role and version line

Starboard is the porting interface: everything platform-specific sits behind it. That covers media playback and
DRM, graphics, audio output, input, system properties, and (before the POSIX migration of Starboard 16–18) memory,
threads, sockets and files.

The [porting guide](https://developers.google.com/youtube/cobalt/docs/starboard/porting) targets "Cobalt 27 …
Starboard 18". Custom ports live in `third_party/starboard/<family>/`, and the NPLB ("No Platform Left Behind")
test suite validates them.

| Branch | `SB_MINIMUM_API_VERSION` / `SB_MAXIMUM_API_VERSION` |
|--------|-----------------------------------------------------|
| `27.lts`, `main` | 18 / 18 ([configuration.h](https://github.com/youtube/cobalt/blob/27.lts/starboard/configuration.h)) |
| `26.eap` | 17 / 17 |
| `25.lts.1+` | 14 / 16 |

Notable changes recorded in the [Starboard CHANGELOG](https://github.com/youtube/cobalt/blob/main/starboard/CHANGELOG.md):

- **Starboard 16–18: POSIX migration.**
  - The changelog says Starboard 16 began adopting POSIX APIs and "Starboard 17 fully switches to POSIX APIs".
  - The 27 LTS notes say Starboard 18 "consolidates the transition to standard POSIX APIs, replacing over a hundred
    proprietary Starboard functions".
- **Starboard 18**
  - Adds `SbMediaCanChangeType()` to support `SourceBuffer.changeType()`, and adds AV2 as a video codec.
  - Removes `SbMediaGetProgressiveBufferBudget` because progressive playback was removed.
  - Asks decode-to-texture YUV planes to use `GL_RED_EXT` or `GL_LUMINANCE` instead of `GL_ALPHA`, to avoid
    green-screen artifacts.
- **Starboard 15** adds `SbPlayerGetAudioConfiguration()`.
- **Starboard 14** makes `SbPlayerCreationParam` and `SbPlayerGetPreferredOutputMode()` always enabled and deprecates
  `SbPlayerOutputModeSupported()`.
- **Starboard 12** introduces `SbPlayerGetPreferredOutputMode()`, `cbcs` support in `SbDrmSystem`, and the
  `encryptionscheme` key-system attribute.

## 4. The SbPlayer model

All of the following comes from
[`starboard/player.h`](https://github.com/youtube/cobalt/blob/27.lts/starboard/player.h) on `27.lts`.

### 4.1 How media flows

```text
Web app (MSE) --> ChunkDemuxer --> media::StarboardRenderer --> SbPlayerBridge --> SbPlayer
 appendBuffer()   (Chromium)       (usually Chrome_InProcGpuThread)                 decode + render
                                                                                    (platform)
```

1. The web application fetches segments and appends them through MSE.
2. Chromium's demuxer extracts elementary-stream access units.
3. `StarboardRenderer` creates an `SbPlayer`. Its header calls it "the entry point for all video playbacks on
   Starboard platforms".
4. The platform owns decoding, A/V sync and presentation. Cobalt pushes samples to it on demand.

### 4.2 Lifecycle and the push contract

| Step | API | Contract |
|------|-----|----------|
| Query | `SbPlayerGetPreferredOutputMode(&param)` | The platform chooses the output mode for these stream parameters |
| Create | `SbPlayerCreate(window, &param, sample_deallocate_func, decoder_status_func, player_status_func, player_error_func, context, provider)` | Returns `kSbPlayerInvalid` if the platform's concurrent-player limit is exceeded |
| Seek | `SbPlayerSeek(player, seek_to_timestamp, ticket)` | Must be called before samples are sent; the `ticket` lets the client drop callbacks from earlier seeks |
| Pull signal | `decoder_status_func(..., kSbPlayerDecoderStateNeedsData, ticket)` | After each signal the client makes **at most one** write call |
| Write | `SbPlayerWriteSamples(player, type, infos, n)` | `n` ranges from 1 to `SbPlayerGetMaximumNumberOfSamplesPerWrite()`; data is copied, then released through `sample_deallocate_func` |
| End | `SbPlayerWriteEndOfStream(player, type)` | Per stream type |
| Control | `SbPlayerSetPlaybackRate`, `SbPlayerSetVolume`, `SbPlayerSetBounds` | Rate 0 pauses; negative rates are not supported |
| Observe | `SbPlayerGetInfo(player, &info)` | Current media timestamp, duration, frame size, paused state, volume, total/dropped/corrupted frame counts |

- **States:**
  - Player: `Initialized`, `Prerolling`, `Presenting`, `EndOfStream`, `Destroyed`.
  - Errors: `kSbPlayerErrorDecode` and `kSbPlayerErrorCapabilityChanged`. The second asks the app to restart
    playback.
- **Threading.** Callbacks run on a thread other than the caller's, and SbPlayer functions must not be called from
  inside them.
- **Samples.** Each `SbPlayerSampleInfo` carries:
  - the buffer and its size;
  - a timestamp in microseconds;
  - optional side data;
  - per-sample audio or video info;
  - `drm_info`, an `SbDrmSampleInfo` that is required for encrypted samples and `NULL` otherwise.
- **Flow control belongs to the platform.**
  - `kSbPlayerWriteDurationLocal` is 0.5 s and `kSbPlayerWriteDurationRemote` is 10 s, the latter for Bluetooth or
    remote audio.
  - The header still says the platform "should achieve flow control by sending `kSbPlayerDecoderStateNeedsData` less
    frequently".

### 4.3 Output modes: decode-to-texture vs punch-out

| Mode | What the platform does | Implications |
|------|------------------------|--------------|
| `kSbPlayerOutputModeDecodeToTexture` | Exposes frames that the app pulls with `SbPlayerGetCurrentFrame()`, which returns an `SbDecodeTarget` (GL texture / EGLImage-like handle, see [`decode_target.h`](https://github.com/youtube/cobalt/blob/27.lts/starboard/decode_target.h)) | Video is composited by the app's GPU pipeline, so it supports transforms and effects. The texture may not be readable (`glReadPixels`) when the content is DRM-protected, and some platforms do not support DRM in this mode |
| `kSbPlayerOutputModePunchOut` | Renders video on a background plane; the app punches an alpha hole in its graphics plane | Video is positioned with `SbPlayerSetBounds(player, z_index, x, y, w, h)`, which may be called up to 60 Hz and must stay in sync with UI composition. This is the usual path for secure hardware video planes |

Since Starboard 12 the platform, not the app, states its preference through `SbPlayerGetPreferredOutputMode()`;
Starboard 14 deprecates the older `SbPlayerOutputModeSupported()`.

### 4.4 The historical URL-based player

- Starboard once had a mode in which the platform received a URL rather than samples. On `19.lts.1+`, `player.h`
  guarded `SbPlayerCreateWithUrl()` behind `SB_HAS(PLAYER_WITH_URL)`
  ([19.lts player.h](https://github.com/youtube/cobalt/blob/19.lts.1%2B/starboard/player.h)).
- The function is absent from core `player.h` in later LTS branches.
- On `main` the concept survives only as a tvOS platform header,
  [`starboard/tvos/shared/media/url_player.h`](https://github.com/youtube/cobalt/blob/main/starboard/tvos/shared/media/url_player.h).
  It declares `SbUrlPlayerCreate` and `SbUrlPlayerExtraInfo`, which fits a platform whose native player (AVPlayer)
  wants a URL rather than demuxed samples.

## 5. Capability queries: SbMediaCanPlay and friends

All of these are in [`starboard/media.h`](https://github.com/youtube/cobalt/blob/27.lts/starboard/media.h).

- **`SbMediaCanPlayMimeAndKeySystem(mime, key_system)`** returns `kSbMediaSupportTypeNotSupported`, `Maybe` or
  `Probably`. These mirror the `canPlayType()` vocabulary.
  - `mime` may include arbitrary parameters such as `codecs` (possibly several, comma-separated) and `channels`.
  - An empty `key_system` means clear content.
  - Key systems may carry attributes. The one that must be supported is `encryptionscheme` (`"cenc"`, `"cbcs"`,
    `"cbcs-1-9"`), reflecting EME's `MediaKeySystemMediaCapability.encryptionScheme`.
  - NPLB includes `SbMediaCanPlayMimeAndKeySystem.ValidatePerformance` to enforce how fast this call is. Platforms
    that fail it can enable `MimeSupportabilityCache` and `KeySystemSupportabilityCache`
    ([CHANGELOG](https://github.com/youtube/cobalt/blob/main/starboard/CHANGELOG.md)).
- **`SbMediaCanChangeType(current_mime, new_mime)`** (Starboard 18) tells the app whether an active `SbPlayer` can
  switch codec or configuration in place, matching
  [`SourceBuffer.changeType()`](https://www.w3.org/TR/media-source/#dom-sourcebuffer-changetype).
- **`SbMediaVideoStreamInfo.max_video_capabilities`** uses the same string format. It lets the app tell the platform
  that it will never adapt above, for example, `width=1920; height=1080; framerate=15;`, so decoders can size their
  resources.
- **Codec enums:**
  - Video: H.264, H.265, MPEG-2, Theora, VC-1, AV1, VP8, VP9, AV2.
  - Audio: AAC, AC-3, E-AC-3, Opus, Vorbis, MP3, FLAC, PCM, IAMF.
- **Audio outputs and buffer budgets.** `SbMediaGetAudioOutputCount()` and `SbMediaGetAudioConfiguration()` describe
  the audio outputs. Buffer-budget functions let the platform cap media memory.

## 6. EME and SbDrm

[`starboard/drm.h`](https://github.com/youtube/cobalt/blob/27.lts/starboard/drm.h) is a thin, asynchronous
translation of EME.

| EME concept | SbDrm equivalent |
|-------------|------------------|
| `MediaKeys` for a key system | `SbDrmCreateSystem(key_system, context, callbacks...)` / `SbDrmDestroySystem` |
| `generateRequest()` | `SbDrmGenerateSessionUpdateRequest(drm_system, ticket, type, initialization_data, initialization_data_size)` |
| `message` event | `SbDrmSessionUpdateRequestFunc`, with request types `License`, `Renewal`, `Release`, `Individualization` |
| `update()` | `SbDrmUpdateSession`, answered by `SbDrmSessionUpdatedFunc` |
| `keystatuseschange` | `SbDrmSessionKeyStatusesChangedFunc` |
| `close()` | `SbDrmCloseSession`, answered by `SbDrmSessionClosedFunc` |
| `setServerCertificate()` | `SbDrmIsServerCertificateUpdatable` / `SbDrmUpdateServerCertificate` |
| `getMetrics` (non-standard) | `SbDrmGetMetrics` |

- The DRM system is bound to playback by passing it in `SbPlayerCreationParam.drm_system`.
- Each encrypted sample carries an `SbDrmSampleInfo`:
  - encryption scheme `AesCtr` (cenc) or `AesCbc` (cbcs);
  - an optional encryption pattern;
  - the IV and key ID;
  - the subsample map.
- Decryption is therefore inside the platform's decode path, so the app never sees clear compressed data.

## 7. Lessons for Alumbra

| Cobalt/Starboard concept | Penumbra design consequence |
|--------------------------|-----------------------------|
| `SbPlayer` push model (`NeedsData` → one write) | The Penumbra player contract should be **push-based**. Umbra owns networking, demux and ABR, and writes encoded access units only when the port asks. The port owns pacing, which keeps backpressure out of Umbra |
| Seek tickets | Every seek/flush carries a monotonically increasing ticket. Callbacks with stale tickets are ignored, which avoids races between in-flight writes and seeks |
| Callbacks on platform threads; no re-entrancy | Penumbra callbacks must be documented as arriving on arbitrary threads, and Umbra posts them to its own task runner. No Penumbra call may be made from inside a callback |
| `SbPlayerGetPreferredOutputMode()` | The port reports its preferred **output mode** per stream: texture/surface (frames composited by Antumbra) or overlay/punch-out (hardware plane positioned by bounds). The web port maps MSE to an overlay-like mode and WebCodecs+WebGL to a texture mode |
| `SbPlayerSetBounds` up to 60 Hz | Overlay-mode ports need a bounds API with z-order that can be called each frame, and a rule for keeping it in sync with UI composition |
| `SbMediaCanPlayMimeAndKeySystem` | Capability probing is a single synchronous query taking the MIME (with `codecs` and other parameters) and key system (with `encryptionscheme`). It answers `canPlayType()`, `isTypeSupported()` and ABR filtering. Like NPLB, Penumbra conformance should enforce its speed, with results cached in Umbra |
| `SbMediaCanChangeType` | A separate in-place codec-switch query lets Umbra decide between reconfiguring and recreating the decoder, for example across ad/content boundaries |
| `max_video_capabilities` | ABR ceilings are passed to the port at creation, so decoders can allocate conservatively |
| `kSbPlayerErrorCapabilityChanged` | Ports need a "capability changed, restart me" error. Typical causes are HDMI hot-plug and audio-output changes |
| `SbDrm` mirrors EME | Penumbra DRM should mirror EME semantics (sessions, messages, key statuses, server certificates), and the DRM handle is passed at player creation. ClearKey/CENC first exercises the whole path before Widevine, FairPlay and PlayReady |
| One `SbPlayer` per playback; creation may fail at a limit | Player instances are per playback, and running out of decoder instances is an expected, reportable error. This matters for ad preloading and picture-in-picture |
| Platform-signalled write duration (local vs remote audio) | Ports report audio-output latency (for example Bluetooth), and Umbra uses it for A/V sync and buffer targets |
| URL player survives only for tvOS | Keep an **optional** URL-based escape hatch for platforms whose best path is a native URL player (AVPlayer for FairPlay, HLS Interstitials and AirPlay). Treat it as an exception with reduced Umbra features, not the default |
| NPLB conformance suite | Penumbra ships a conformance test suite that every port must pass, as the definition of "done" for a port |

## References

- Google. *Cobalt overview*. https://developers.google.com/youtube/cobalt/docs/overview
- Google. *Cobalt* (landing page). https://developers.google.com/youtube/cobalt
- Google. *Cobalt 26 EAP release notes*. https://developers.google.com/youtube/cobalt/docs/releases/26.EAP
- Google. *Cobalt 27 LTS Stable (27.LTS.10) release notes*. https://developers.google.com/youtube/cobalt/docs/releases/27.LTS.10
- Google. *Porting Cobalt / Starboard*. https://developers.google.com/youtube/cobalt/docs/starboard/porting
- youtube/cobalt repository. https://github.com/youtube/cobalt
- `starboard/player.h` (27.lts). https://github.com/youtube/cobalt/blob/27.lts/starboard/player.h
- `starboard/media.h` (27.lts). https://github.com/youtube/cobalt/blob/27.lts/starboard/media.h
- `starboard/drm.h` (27.lts). https://github.com/youtube/cobalt/blob/27.lts/starboard/drm.h
- `starboard/decode_target.h` (27.lts). https://github.com/youtube/cobalt/blob/27.lts/starboard/decode_target.h
- `starboard/configuration.h` (27.lts). https://github.com/youtube/cobalt/blob/27.lts/starboard/configuration.h
- Starboard CHANGELOG. https://github.com/youtube/cobalt/blob/main/starboard/CHANGELOG.md
- Cobalt 25 → 27 migration guide. https://github.com/youtube/cobalt/blob/27.lts/starboard/doc/eap/25_to_27.md
- `starboard/player.h` (19.lts.1+, `SbPlayerCreateWithUrl`). https://github.com/youtube/cobalt/blob/19.lts.1%2B/starboard/player.h
- `starboard/tvos/shared/media/url_player.h` (main). https://github.com/youtube/cobalt/blob/main/starboard/tvos/shared/media/url_player.h
- `media/starboard/starboard_renderer.h` (main). https://github.com/youtube/cobalt/blob/main/media/starboard/starboard_renderer.h
- Classic media glue, `cobalt/media/base` (25.lts.1+). https://github.com/youtube/cobalt/tree/25.lts.1%2B/cobalt/media/base
- W3C. *Media Source Extensions, `changeType()`*. https://www.w3.org/TR/media-source/#dom-sourcebuffer-changetype
- Collabora. *Re-engineering YouTube for the living room: Bringing Google's Chrobalt to the RDK* (2026-09-11). https://www.collabora.com/news-and-blog/news-and-events/re-engineering-youtube-for-the-living-room-bringing-google-chrobalt-to-the-rdk.html
