# HTMLMediaElement conformance

Generated from `umbra/player/conformance.toml` by `tools/api_conformance/generate.py`.
Never edit this page by hand ([ADR-0022](../adr/0022-documentation-site-and-api-reference.md)).
The generator fails when a public member of `Player` has no row, or when an implemented row names a test that does not exist, so the table cannot drift from the code ([ADR-0023](../adr/0023-public-api-surface-and-media-error.md)).

The C++ names map mechanically to the bindings: `Play()` becomes `play()`, `current_time()` becomes `currentTime`, and `set_current_time()` becomes the setter of that property. A `stable` member is not expected to change before 1.0; an `experimental` member may change in any release.

## Attributes

| Name | C++ | Binding | Status | Stability | Tests | Notes |
|------|-----|---------|--------|-----------|-------|-------|
| `src` | `src`, `set_src` | `src` | implemented since M1 | stable | `PlayerTest.SettingSrcRunsTheLoadAlgorithm`<br>`PlayerTest.AnEmptySrcFailsAsUnsupported` |  |
| `networkState` | `network_state` | `networkState` | implemented since M1 | stable | `PlayerTest.InitialStateMatchesTheHtmlDefaults` |  |
| `paused` | `paused` | `paused` | implemented since M1 | stable | `PlayerTest.PauseAbortsAPendingPlay` |  |
| `ended` | `ended` | `ended` | implemented since M1 | stable | `PlayerTest.EndedIsTrueAfterPlaybackReachesTheEnd` |  |
| `autoplay` | `autoplay`, `set_autoplay` | `autoplay` | implemented since M1 | stable | `PlayerTest.FlagsAndHintsRoundTrip`<br>`PlayerTest.AutoplayStartsPlaybackWhenEnoughDataIsBuffered` |  |
| `loop` | `loop`, `set_loop` | `loop` | implemented since M1 | stable | `PlayerTest.FlagsAndHintsRoundTrip` |  |
| `currentTime` | `current_time`, `set_current_time` | `currentTime` | implemented since M1 | stable | `PlayerTest.CurrentTimeSetBeforeMetadataIsTheDefaultStartPosition`<br>`PlayerTest.CurrentTimeRejectsNonFiniteValues`<br>`PlayerTest.SeekingUpdatesPositionAndSeekingAtOnce` |  |
| `seeking` | `seeking` | `seeking` | implemented since M1 | stable | `PlayerTest.SeekingUpdatesPositionAndSeekingAtOnce` |  |
| `duration` | `duration` | `duration` | implemented since M1 | stable | `PlayerTest.DurationIsNanBeforeMetadata`<br>`PlayerTest.DurationIsInfinityWhenTheStreamIsUnbounded` |  |
| `buffered` | `buffered` | `buffered` | implemented since M1 | experimental | `PlayerTest.BufferedFollowsTheDataWrittenToThePort`<br>`PlayerTest.BufferedStartsAtTheSeekTarget` | At most one range until #91. |
| `seekable` | `seekable` | `seekable` | implemented since M1 | experimental | `PlayerTest.SeekableCoversTheKnownDuration` | Live DVR windows follow with #91. |
| `readyState` | `ready_state` | `readyState` | implemented since M1 | stable | `PlayerTest.ReadyStateRisesWithTheBufferedData` |  |
| `preload` | `preload`, `set_preload` | `preload` | implemented since M1 | stable | `PlayerTest.FlagsAndHintsRoundTrip` |  |
| `playbackRate` | `playback_rate`, `set_playback_rate` | `playbackRate` | implemented since M1 | stable | `PlayerTest.PlaybackRateAcceptsNonNegativeFiniteValues` | Negative rates are rejected (ADR-0024). |
| `defaultPlaybackRate` | `default_playback_rate`, `set_default_playback_rate` | `defaultPlaybackRate` | implemented since M1 | stable | `PlayerTest.DefaultPlaybackRateAppliesOnTheNextLoad` |  |
| `preservesPitch` | `preserves_pitch`, `set_preserves_pitch` | `preservesPitch` | implemented since M1 | experimental | `PlayerTest.FlagsAndHintsRoundTrip` | Stored until #90 and #92 apply it. |
| `volume` | `volume`, `set_volume` | `volume` | implemented since M1 | stable | `PlayerTest.VolumeIsLimitedToTheUnitInterval` |  |
| `muted` | `muted`, `set_muted` | `muted` | implemented since M1 | stable | `PlayerTest.MutedKeepsTheVolume` |  |
| `error` | `error` | `error` | implemented since M1 | stable | `PlayerTest.ErrorCarriesCodeMessageAndDetail`<br>`PlayerTest.LoadClearsTheError` |  |
| `currentSrc` | - | - | absent | - | - | Outside the v1 subset; follows with `<source>` children. |
| `crossOrigin` | - | - | absent | - | - | Settings cover it (ADR-0023). |
| `srcObject` | - | - | absent | - | - | Outside the v1 subset. |
| `controls` | - | - | absent | - | - | Absent for good: UI belongs to Antumbra (ADR-0007). |
| `played` | - | - | absent | - | - | Dropped from M1 by ADR-0023. |
| `textTracks` | - | - | absent | - | - | Planned with #129. |
| `audioTracks` | - | - | absent | - | - | Planned with #125. |
| `videoTracks` | - | - | absent | - | - | Planned with #125. |
| `mediaKeys` | - | - | absent | - | - | Planned with #235. |

## Methods

| Name | C++ | Binding | Status | Stability | Tests | Notes |
|------|-----|---------|--------|-----------|-------|-------|
| `load` | `Load` | `load` | implemented since M1 | stable | `PlayerTest.LoadWithoutSrcSelectsNoResource`<br>`PlayerTest.LoadAbortsAPendingPlay`<br>`PlayerTest.LoadClearsTheError` |  |
| `canPlayType` | `CanPlayType` | `canPlayType` | implemented since M1 | stable | `CanPlayTypeTest.KnownCodecsOnAKnownContainerAreProbablySupported`<br>`CanPlayTypeTest.AContainerWithoutCodecsIsOnlyMaybeSupported`<br>`CanPlayTypeTest.AnUnknownContainerIsNotSupported` |  |
| `play` | `Play` | `play` | implemented since M1 | stable | `PlayerTest.PlayCallbackRunsOnceFromDispatchEvents`<br>`PlayerTest.PlayFailsWithNotSupportedWhenTheSourceCannotBeOpened`<br>`PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt` |  |
| `pause` | `Pause` | `pause` | implemented since M1 | stable | `PlayerTest.PauseAbortsAPendingPlay` |  |
| `fastSeek` | `FastSeek` | `fastSeek` | implemented since M1 | experimental | `PlayerTest.FastSeekSeeksLikeTheCurrentTimeSetter` | Seeks exactly until #91 adds a keyframe index. |
| `getVideoPlaybackQuality` | - | - | absent | - | - | Dropped from M1 by ADR-0023; dropped frames come from `stats()` meanwhile. |
| `getStartDate` | - | - | absent | - | - | Joins the subset with the live sources (#107, #116). |
| `addTextTrack` | - | - | absent | - | - | Planned with #129. |

## Events

| Name | C++ | Binding | Status | Stability | Tests | Notes |
|------|-----|---------|--------|-----------|-------|-------|
| `loadstart` | - | - | absent | - | - | Planned with #96. |
| `progress` | - | - | absent | - | - | Planned with #96. |
| `suspend` | - | - | absent | - | - | Planned with #96. |
| `abort` | - | - | absent | - | - | Planned with #96. |
| `error` | - | - | absent | - | - | Planned with #96. |
| `emptied` | - | - | absent | - | - | Planned with #96. |
| `stalled` | - | - | absent | - | - | Planned with #96. |
| `loadedmetadata` | - | - | absent | - | - | Planned with #96. |
| `loadeddata` | - | - | absent | - | - | Planned with #96. |
| `canplay` | - | - | absent | - | - | Planned with #96. |
| `canplaythrough` | - | - | absent | - | - | Planned with #96. |
| `playing` | - | - | absent | - | - | Planned with #96. |
| `waiting` | - | - | absent | - | - | Planned with #96. |
| `seeking` | - | - | absent | - | - | Planned with #96. |
| `seeked` | - | - | absent | - | - | Planned with #96. |
| `ended` | - | - | absent | - | - | Planned with #96. |
| `durationchange` | - | - | absent | - | - | Planned with #96. |
| `timeupdate` | - | - | absent | - | - | Planned with #96. |
| `play` | - | - | absent | - | - | Planned with #96. |
| `pause` | - | - | absent | - | - | Planned with #96. |
| `ratechange` | - | - | absent | - | - | Planned with #96. |
| `resize` | - | - | absent | - | - | Planned with #96. |
| `volumechange` | - | - | absent | - | - | Planned with #96. |

## Extensions

| Name | C++ | Binding | Status | Stability | Tests | Notes |
|------|-----|---------|--------|-----------|-------|-------|
| `Player::Create` | `Create` | `create` | implemented since M1 | experimental | `PlayerTest.CreateRejectsAnInvalidPrerollAndAMissingRunner`<br>`PlayerTest.PublicCreateBuildsAPlayerWhoseLoadsAreUnsupported` | Cannot open a resource until a port and data sources land. |
| `Player::DispatchEvents` | `DispatchEvents` | `dispatchEvents` | implemented since M1 | experimental | `PlayerTest.PlayCallbackRunsOnceFromDispatchEvents`<br>`PlayerTest.DestroyingThePlayerFromACallbackDropsTheRestOfTheQueue`<br>`PlayerTest.EventsAvailableHookRunsWhenTheQueueBecomesNonEmpty` |  |
| `MediaError.detail` | - | - | implemented since M1 | experimental | `MediaErrorTest.MessageStartsWithTheCatalogueCode`<br>`PlayerTest.ErrorCarriesCodeMessageAndDetail` |  |
| `AddEventListener` | - | - | absent | - | - | Planned with #96. |
| `abr` | - | - | absent | - | - | Appears when ABR is built (#11, #125). |
| `stats` | - | - | absent | - | - | Appears when statistics are built (#126). |
| `settings` | - | - | absent | - | - | Appears with the per-player settings layer (ADR-0017). |
| `drm` | - | - | absent | - | - | Appears when DRM configuration is built. |
