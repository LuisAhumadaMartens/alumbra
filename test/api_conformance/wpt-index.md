# WPT index

Maps every web-platform-tests case under `html/semantics/embedded-content/media-elements` to what the API conformance suite does with it ([#100](https://github.com/LuisAhumadaMartens/alumbra/issues/100), [ADR-0023](../../docs/adr/0023-public-api-surface-and-media-error.md)). Paths are relative to that directory. `tools/api_conformance/check_wpt_index.py` runs as a ctest and fails when a row names a test that does not exist, when a ported test is missing from this page, or when a not-applicable or deferred row has no reason.

Pinned to web-platform-tests `master` as read on 2 November 2026. Files that are not tests (`WEB_FEATURES.yml`, `pitch-detector.js`, `resources/`, `*.headers`) are left out. A directory ending in `/*` stands for every test inside it.

| Status | Meaning |
|--------|---------|
| adopted | Same assertions, same inputs. |
| adapted | Same intent, reshaped for the C++ API (events pulled with `DispatchEvents`, setters returning `false` instead of throwing, the fake port instead of a media file). The Reason column says how. |
| deviation | Ported, but Alumbra fails the original assertion today. The failing test is disabled, named here and tracked by an issue; passing neighbours keep the rest of the coverage. |
| not-applicable | Cannot apply to this API. The Reason column says why. |
| deferred | Applies, but needs decoded media on a real port. The Reason column says what is missing. |

Totals: 3 adopted, 54 adapted, 9 deviation, 91 not-applicable, 3 deferred (160 entries).

The expected event sequences that the suite replays are recorded in `traces/`; see [README.md](README.md) for their provenance.

| WPT path | Status | Tests | Reason |
|----------|--------|-------|--------|
| `audio_controls_present-manual.html` | not-applicable | - | The controls attribute is absent for good: UI belongs to Antumbra (ADR-0007). Manual test: a person has to listen to or look at the output. |
| `audio_loop_base.html` | deferred | - | Needs a decoded media resource and real playback on a port. The fake port cannot decode, so the equivalent fake-port flow is named in the neighbouring rows; the desktop port does not play media yet (follow-up on #100). |
| `audio_loop_seek_to_eos.html` | adapted | `WptAudioLoopSeekToEos.SeekingToTheEndOfALoopingElementKeepsItPlaying` | The fake port stands in for the decoded audio and the play() promise is replaced by the play callback. |
| `audio_muted_overriding_volume-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `audio_muted_present-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `audio_volume_check.html` | adapted | `WptVolumeCheck.InitialValueIsWithinTheUnitInterval`<br>`WptVolumeCheck.ValuesInsideTheRangeAreStoredAndOutsideAreRejected` | IndexSizeError becomes a false return from set_volume(). |
| `audio_volume_loudest-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `audio_volume_silent-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `autoplay-allowed-by-permissions-policy-attribute-redirect-on-load.https.sub.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-allowed-by-permissions-policy-attribute.https.sub.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-allowed-by-permissions-policy.https.sub.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-default-permissions-policy.https.sub.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-disabled-by-permissions-policy.https.sub.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-supported-by-permissions-policy.html` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `autoplay-with-broken-track.html` | not-applicable | - | Text tracks are absent in the v1 subset (planned with #129). |
| `controlsList.tentative.html` | not-applicable | - | The controls attribute is absent for good: UI belongs to Antumbra (ADR-0007). |
| `crashtests/no-blocking-loads-gc-crash.html` | not-applicable | - | Garbage-collection crash test for DOM elements. |
| `crashtests/reinsert-audio-element-onerror-crash.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `error-codes/error.html` | adapted | `WptErrorCodesError.InitialValueIsNull`<br>`WptErrorCodesError.NullAfterSuccessfulLoad`<br>`WptErrorCodesError.AfterSettingSrcToTheEmptyStringIsSrcNotSupported`<br>`WptPublicPlayer.AnEmptySrcFailsWithSrcNotSupported` | The error event is observed through DispatchEvents. The audio and video variants collapse into one because the C++ API has no tag name. |
| `error-sequence.html` | deviation | `WptErrorSequence.EmptySrcQueuesLoadstartThenErrorThenLaterEvents`<br>`WptErrorSequence.ErrorAttributeIsSetSynchronouslyOnADecodeError`<br>`WptErrorSequence.ADecodeErrorDoesNotRejectAPendingPlay`<br>`WptErrorSequence.DISABLED_EmptySrcQueuesVolumechangeBeforeLoadstart` | Known deviation #408 (loadstart is queued synchronously). The MediaSource subtests need MSE, which the v1 subset does not have, and are adapted to source errors injected through the fake source. |
| `event_canplay.html` | adapted | `WptEventCanplay.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_canplay_noautoplay.html` | adapted | `WptEventCanplay.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_canplaythrough.html` | adapted | `WptEventCanplaythrough.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_canplaythrough_noautoplay.html` | adapted | `WptEventCanplaythrough.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadeddata.html` | adapted | `WptEventLoadeddata.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadeddata_noautoplay.html` | adapted | `WptEventLoadeddata.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadedmetadata.html` | adapted | `WptEventLoadedmetadata.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadedmetadata_noautoplay.html` | adapted | `WptEventLoadedmetadata.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadstart.html` | adapted | `WptEventLoadstart.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_loadstart_noautoplay.html` | adapted | `WptEventLoadstart.FiresAfterSettingSrcWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_order_canplay_canplaythrough.html` | adapted | `WptEventOrder.CanplayThenCanplaythrough` | - |
| `event_order_canplay_playing.html` | adapted | `WptEventOrder.CanplayIsFiredBeforePlayingWhenAutoplayStarts` | - |
| `event_order_durationchange_resize_loadedmetadata.html` | adapted | `WptEventOrder.DurationchangeThenResizeThenLoadedmetadata` | - |
| `event_order_loadedmetadata_loadeddata.html` | adapted | `WptEventOrder.LoadedmetadataThenLoadeddata` | - |
| `event_order_loadstart_progress.html` | adapted | `WptEventOrder.LoadstartIsFirstAndProgressFollowsTheData` | Progress arrives on the 350 ms timer, which the fake clock advances. |
| `event_pause.html` | adapted | `WptEventPause.FiresWhenPlaybackIsPaused` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_pause_noautoplay.html` | adapted | `WptEventPause.FiresWhenPlaybackIsPaused` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_play.html` | adapted | `WptEventPlay.FiresWhenAutoplayOrPlayStartsPlayback` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_play_noautoplay.html` | adapted | `WptEventPlay.FiresWhenAutoplayOrPlayStartsPlayback` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_playing.html` | adapted | `WptEventPlaying.FiresWhenAutoplayOrPlayStartsPlayback` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_playing_noautoplay.html` | adapted | `WptEventPlaying.FiresWhenAutoplayOrPlayStartsPlayback` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_progress.html` | adapted | `WptEventProgress.FiresWhileDataArrivesWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_progress_noautoplay.html` | adapted | `WptEventProgress.FiresWhileDataArrivesWithAndWithoutAutoplay` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_timeupdate.html` | adapted | `WptEventTimeupdate.FiresWhilePlaybackProgresses` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_timeupdate_noautoplay.html` | adapted | `WptEventTimeupdate.FiresWhilePlaybackProgresses` | The autoplay and noautoplay files collapse into one loop over the autoplay flag. |
| `event_volumechange.html` | adapted | `WptEventVolumechange.FiresWhenVolumeOrMutedChanges`<br>`WptEventVolumechange.EventsCarryTheStateAfterEachSetter`<br>`WptEventVolumechange.SettingTheSameValueDoesNotFireTheEvent`<br>`WptEventVolumechange.RepeatedChangesFireRepeatedly`<br>`WptEventVolumechange.ChangesBeforeALoadDoNotFireTheEvent` | The muted content attribute subtests need a DOM and are left out; the rest keep their assertions. |
| `historical.html` | not-applicable | - | Asserts that removed IDL members are undefined on the element. The C++ API never had them and the conformance table lists only members it implements. |
| `interfaces/HTMLElement/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `interfaces/TextTrack/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `interfaces/TextTrackCue/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `interfaces/TextTrackCueList/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `interfaces/TextTrackList/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `interfaces/TrackEvent/*` | not-applicable | - | IDL interface tests. They run against the TypeScript binding from M4 (ADR-0023); text tracks are absent in the v1 subset. |
| `loading-the-media-resource/autoplay-overrides-preload.html` | adapted | `WptAutoplayOverridesPreload.AutoplayStartsPlaybackWhateverThePreloadHint` | The audio resource is the fake source, opened by the harness. |
| `loading-the-media-resource/load-events-networkState.html` | adapted | `WptLoadEventsNetworkState.LoadFromIdleFiresAbortThenEmptied`<br>`WptLoadEventsNetworkState.EmptiedIsFiredBeforeTimeupdate`<br>`WptLoadEventsNetworkState.LoadWhileLoadingFiresAbortThenEmptied`<br>`WptLoadEventsNetworkState.LoadFromNoSourceFiresOnlyEmptied` | The four networkState subtests keep their expectations; suspend and the data: URL are replaced by the fake source. |
| `loading-the-media-resource/load-removes-queued-error-event.html` | deviation | `WptLoadRemovesQueuedErrorEvent.DISABLED_LoadInLoadstartRemovesTheQueuedError`<br>`WptLoadRemovesQueuedErrorEvent.DISABLED_ANewSrcInLoadstartLeavesNoPendingError` | Known deviation #409. The source element subtest needs source children. |
| `loading-the-media-resource/media-blob-url-revoked.window.js` | not-applicable | - | Blob URLs belong to the browser URL store. |
| `loading-the-media-resource/resource-selection-candidate-insert-before.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-candidate-moved.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-candidate-remove-addEventListener.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-candidate-remove-no-listener.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-candidate-remove-onerror.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-currentSrc.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-encoding-parse.html` | not-applicable | - | Parses src with the document encoding; the C++ API takes a URL string. |
| `loading-the-media-resource/resource-selection-invoke-audio-constructor-no-src.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-audio-constructor.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-in-sync-event.html` | not-applicable | - | Depends on a synchronous DOM click handler and on removing the src attribute, which the C++ API cannot do. |
| `loading-the-media-resource/resource-selection-invoke-insert-fragment-into-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-into-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-into-iframe.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-parent-into-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-source-in-div.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-source-in-namespace.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-source-networkState.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-source-not-in-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-insert-source.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-load.html` | deviation | `WptResourceSelectionInvoke.LoadWithoutSrcSettlesAtNetworkEmpty`<br>`WptResourceSelectionInvoke.DISABLED_LoadWithoutSrcIsNoSourceUntilTheTaskEnds` | Known deviation #412: no transient NETWORK_NO_SOURCE. |
| `loading-the-media-resource/resource-selection-invoke-move-source-audio.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-move-source-in-div.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-move-source-in-namespace.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-move-source-not-in-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-move-source-same-parent.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-move-source.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-pause-networkState.html` | adapted | `WptResourceSelectionInvoke.PauseWhenNetworkStateIsNotEmptyDoesNotReload` | A data: URL becomes an empty src, which fails the same way. |
| `loading-the-media-resource/resource-selection-invoke-pause.html` | deviation | `WptResourceSelectionInvoke.PauseWithoutSrcSettlesAtNetworkEmpty`<br>`WptResourceSelectionInvoke.DISABLED_PauseWithoutSrcIsNoSourceUntilTheTaskEnds` | Known deviation #412: no transient NETWORK_NO_SOURCE. |
| `loading-the-media-resource/resource-selection-invoke-play.html` | deviation | `WptResourceSelectionInvoke.PlayWithoutSrcSettlesAtNetworkEmpty`<br>`WptResourceSelectionInvoke.DISABLED_PlayWithoutSrcIsNoSourceUntilTheTaskEnds` | Known deviation #412: no transient NETWORK_NO_SOURCE. |
| `loading-the-media-resource/resource-selection-invoke-remove-from-document-networkState.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-remove-from-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-remove-src.html` | not-applicable | - | Removes the src attribute, which the C++ API cannot do (an empty src is a value, not an absence). |
| `loading-the-media-resource/resource-selection-invoke-set-src-in-namespace.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-set-src-networkState.html` | deviation | `WptResourceSelectionInvoke.DISABLED_SettingSrcAfterPlayLeavesTheElementPaused`<br>`WptResourceSelectionInvoke.DISABLED_SettingAnEmptySrcAfterPlayLeavesTheElementPaused` | Known deviation #415 (paused stays false) and #412 (no transient NO_SOURCE after play()). |
| `loading-the-media-resource/resource-selection-invoke-set-src-not-in-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `loading-the-media-resource/resource-selection-invoke-set-src.html` | adapted | `WptResourceSelectionInvoke.SettingSrcInvokesTheLoadAlgorithm`<br>`WptResourceSelectionInvoke.AnEmptySrcFiresLoadstartAndNeverLoads` | Observes loadstart through DispatchEvents instead of the window load event. |
| `loading-the-media-resource/resource-selection-pointer-control.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-insert-br.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-insert-source.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-insert-text.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-remove-source-after.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-remove-source.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-pointer-remove-text.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-remove-source.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-remove-src.html` | not-applicable | - | Removes the src attribute, which the C++ API cannot do. |
| `loading-the-media-resource/resource-selection-resumes-onload.html` | not-applicable | - | Resumes selection on the document load event. |
| `loading-the-media-resource/resource-selection-source-media-env-change.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/resource-selection-source-media.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `loading-the-media-resource/stable-state-beforeunload-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. It also needs a document. |
| `loading-the-media-resource/stable-state-dialogs-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. It also needs a document. |
| `loading-the-media-resource/stable-state-print-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. It also needs a document. |
| `location-of-the-media-resource/currentSrc.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `media-playback-while-not-visible-permission-policy/*` | not-applicable | - | Document-level Permissions Policy. The C++ API takes the platform decision through PlayerOptions::allow_playback, which PlayerTest.PlayIsRefusedWhenPlatformPolicyForbidsIt covers. |
| `media_fragment_seek.html` | not-applicable | - | Media fragment URIs (#t=) are outside the v1 subset. |
| `mime-types/canPlayType.html` | deviation | `WptMimeTypesCanPlayType.UnknownContainersAreNotSupported`<br>`WptMimeTypesCanPlayType.AKnownContainerWithoutCodecsIsMaybe`<br>`WptMimeTypesCanPlayType.KnownCodecsOnAKnownContainerAreProbablySupported`<br>`WptMimeTypesCanPlayType.ABogusCodecIsNotSupported`<br>`WptMimeTypesCanPlayType.CodecOrderDoesNotChangeTheAnswer`<br>`WptMimeTypesCanPlayType.DISABLED_ADanglingParameterIsMaybe` | Known deviation #414. Only the containers and codecs Alumbra implements are asserted; the rest of the WPT table is optional there too. |
| `networkState_during_loadstart.html` | adapted | `WptNetworkState.IsLoadingDuringLoadstartAndProgress` | Reads the state carried by the event. |
| `networkState_during_progress.html` | adapted | `WptNetworkState.IsLoadingDuringLoadstartAndProgress` | Reads the state carried by the event. |
| `networkState_initial.html` | adopted | `WptNetworkState.InitialValueIsEmpty`<br>`WptPublicPlayer.DefaultsMatchTheHtmlInitialValues` | - |
| `no-autoplay-audio-history-back-does-not-play.html` | not-applicable | - | Needs history navigation. |
| `offsets-into-the-media-resource/currentTime-move-within-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `offsets-into-the-media-resource/currentTime.html` | deviation | `WptOffsetsCurrentTime.InitialValueIsZero`<br>`WptOffsetsCurrentTime.SettingItAtHaveNothingKeepsTheValueWithoutSeeking`<br>`WptOffsetsCurrentTime.SettingItAfterMetadataStartsSeeking`<br>`WptOffsetsCurrentTime.DISABLED_NumberMaxValueIsKeptAtHaveNothing` | Known deviation #410: the value before metadata is clamped to the MediaTime range. The adapted test uses 1e6 seconds. |
| `offsets-into-the-media-resource/duration.html` | adopted | `WptOffsetsDuration.InitialValueIsNan`<br>`WptPublicPlayer.DefaultsMatchTheHtmlInitialValues` | - |
| `offsets-into-the-media-resource/fragmented-mp4-end.html` | deferred | - | Needs a decoded media resource and real playback on a port. The fake port cannot decode, so the equivalent fake-port flow is named in the neighbouring rows; the desktop port does not play media yet (follow-up on #100). |
| `paused_false_during_play.html` | adapted | `WptPausedDuringEvents.IsFalseDuringPlayAndTrueDuringPause` | Reads the state carried by the event. |
| `paused_true_during_pause.html` | adapted | `WptPausedDuringEvents.IsFalseDuringPlayAndTrueDuringPause` | Reads the state carried by the event. |
| `played-loop.html` | not-applicable | - | The played attribute was dropped from the v1 subset by ADR-0023. |
| `playing-the-media-resource/loop-from-ended.tentative.html` | deviation | `WptLoopFromEnded.PlayWithLoopSetAfterTheEndSeeksBackToTheStart`<br>`WptLoopFromEnded.DISABLED_SettingLoopAfterTheEndClearsEnded` | Known deviation #411: ended stays true after loop is set. |
| `playing-the-media-resource/pause-move-to-other-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/pause-move-within-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/pause-remove-from-document-different-load.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/pause-remove-from-document-networkState.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/pause-remove-from-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/play-in-detached-document.html` | not-applicable | - | Needs a DOM: elements, document moves or source children, which the C++ API does not have. |
| `playing-the-media-resource/playbackRate.html` | adapted | `WptPlayingPlaybackRate.InitialValueIsOne`<br>`WptPlayingPlaybackRate.SmallAndLargePositiveRatesFireRatechange`<br>`WptPlayingPlaybackRate.ZeroFiresRatechange`<br>`WptPlayingPlaybackRate.NegativeRatesAreRejectedAndLeaveTheRateAlone`<br>`WptPublicPlayer.NonFiniteVolumeAndNegativeRateAreRejected` | NotSupportedError becomes a false return from set_playback_rate(); negative rates are rejected by design (ADR-0024). |
| `preload_reflects_none_autoplay.html` | adapted | `WptPreloadReflection.NoneIsKeptEvenWhenAutoplayIsSet` | Sets the flags through the setters instead of content attributes. |
| `preserves-pitch.html` | adapted | `WptPreservesPitch.DefaultsToTrueAndCanBeToggled` | The value is stored until #90 and #92 apply it. |
| `ready-states/autoplay-hidden.optional.html` | not-applicable | - | Depends on page visibility, which the C++ API does not know. |
| `ready-states/autoplay-with-slow-text-tracks.html` | not-applicable | - | Text tracks are absent in the v1 subset (planned with #129). |
| `ready-states/autoplay.html` | adapted | `WptReadyStatesAutoplay.AutoplayFiresCanplayCanplaythroughPlayPlaying`<br>`WptReadyStatesAutoplay.AutoplayAndLoadFiresTheSameSequence`<br>`WptReadyStatesAutoplay.AutoplayAndPlayFiresPlayBeforeCanplay`<br>`WptReadyStatesAutoplay.AutoplayAndPauseDoNotStartPlayback` | The fifth subtest (removal from the document) needs a DOM. |
| `readyState_during_canplay.html` | adapted | `WptReadyState.MatchesTheEventThatIsFired` | Reads the state carried by the event. |
| `readyState_during_canplaythrough.html` | adapted | `WptReadyState.MatchesTheEventThatIsFired` | Reads the state carried by the event. |
| `readyState_during_loadeddata.html` | adapted | `WptReadyState.MatchesTheEventThatIsFired` | Reads the state carried by the event. |
| `readyState_during_loadedmetadata.html` | adapted | `WptReadyState.MatchesTheEventThatIsFired` | Reads the state carried by the event. |
| `readyState_during_playing.html` | adapted | `WptReadyState.IsAtLeastHaveFutureDataDuringPlaying` | Reads the state carried by the event. |
| `readyState_initial.html` | adopted | `WptReadyState.InitialValueIsHaveNothing`<br>`WptPublicPlayer.DefaultsMatchTheHtmlInitialValues` | - |
| `replay.html` | adapted | `WptReplay.PlayingAgainAfterTheEndReachesTheEndAgain` | Plays 0.6 s of fake media instead of seeking to 4 s of a real file. |
| `seeking/seek-to-currentTime.html` | adapted | `WptSeekingSeekToCurrentTime.FiresSeekingTimeupdateSeekedInOrder` | Other events (canplay after the seek) are filtered out, as in the original, which listens to three types only. |
| `seeking/seek-to-max-value.htm` | adapted | `WptSeekingSeekToMaxValue.ClampsToTheEndOfTheSeekableRange` | - |
| `seeking/seek-to-negative-time.htm` | adapted | `WptSeekingSeekToNegativeTime.ClampsToTheStartOfTheSeekableRange` | - |
| `src_object_blob.html` | not-applicable | - | srcObject is absent in the v1 subset. |
| `src_reflects_attribute_not_source_elements.html` | not-applicable | - | Needs source children, which are outside the v1 subset (the conformance table marks currentSrc absent). |
| `track/track-element/*` | not-applicable | - | Text tracks are absent in the v1 subset (planned with #129). |
| `user-interface/muted-playbackrate.tentative.html` | not-applicable | - | Tentative and user-agent defined: muted() reflects only the explicit state in Alumbra, not the playback rate. |
| `user-interface/muted.html` | adapted | `WptMutedUserInterface.TogglingMutedRoundTripsAndSurvivesALoad` | The content attribute, defaultMuted and parser-created subtests need a DOM; the setter and load subtests are kept. |
| `video_controls_present-manual.html` | not-applicable | - | The controls attribute is absent for good: UI belongs to Antumbra (ADR-0007). Manual test: a person has to listen to or look at the output. |
| `video_loop_base.html` | deferred | - | Needs a decoded media resource and real playback on a port. The fake port cannot decode, so the equivalent fake-port flow is named in the neighbouring rows; the desktop port does not play media yet (follow-up on #100). |
| `video_muted_overriding_volume-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `video_muted_present-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `video_volume_check.html` | adapted | `WptVolumeCheck.InitialValueIsWithinTheUnitInterval`<br>`WptVolumeCheck.ValuesInsideTheRangeAreStoredAndOutsideAreRejected` | IndexSizeError becomes a false return from set_volume(). |
| `video_volume_loudest-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `video_volume_silent-manual.html` | not-applicable | - | Manual test: a person has to listen to or look at the output. |
| `volume_nonfinite.html` | adapted | `WptVolumeNonfinite.NonFiniteVolumesAreRejectedAndLeaveTheVolumeAlone`<br>`WptPublicPlayer.NonFiniteVolumeAndNegativeRateAreRejected` | TypeError becomes a false return from set_volume(). |
| `volumechange-ratechange-queue-task.html` | adapted | `WptVolumechangeRatechangeQueueTask.MutedFiresVolumechangeFromAQueuedTask`<br>`WptVolumechangeRatechangeQueueTask.VolumeFiresVolumechangeFromAQueuedTask`<br>`WptVolumechangeRatechangeQueueTask.PlaybackRateFiresRatechangeFromAQueuedTask`<br>`WptVolumechangeRatechangeQueueTask.DefaultPlaybackRateFiresRatechangeFromAQueuedTask` | Events are delivered by DispatchEvents, so none can fire inside the setter. |
