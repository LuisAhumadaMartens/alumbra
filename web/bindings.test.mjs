import assert from "node:assert/strict";
import { test } from "node:test";

import {
  AlumbraPlayer,
  MediaPayloadTransfer,
  mapErrorCategory,
  mapMediaErrorCode
} from "./alumbra.mjs";

function createModule() {
  const memory = new ArrayBuffer(1024 * 1024);
  const module = {
    HEAPU8: new Uint8Array(memory),
    HEAPU32: new Uint32Array(memory),
    HEAPF64: new Float64Array(memory),
    HEAPU64: new BigUint64Array(memory),
    allocations: new Set(),
    callbacks: new Map(),
    nextAddress: 64,
    nextHandle: 1n,
    handles: new Set(),
    tracks: new Map(),
    nextTrack: 1n,
    pendingEvent: false,
    eventType: 17,
    _malloc(size) {
      const pointer = (this.nextAddress + 7) & ~7;
      this.nextAddress = pointer + size;
      this.allocations.add(pointer);
      return pointer;
    },
    _free(pointer) { this.allocations.delete(pointer); },
    addFunction(callback) {
      const pointer = this.callbacks.size + 1;
      this.callbacks.set(pointer, callback);
      return pointer;
    },
    removeFunction(pointer) { this.callbacks.delete(pointer); },
    _alumbra_c_api_version() { return 1; },
    _alumbra_negotiate_version(pointer) {
      const index = pointer >>> 2;
      if (this.HEAPU32[index] < 12 || this.HEAPU32[index + 1] !== 1) return 3;
      this.HEAPU32[index + 2] = 1;
      return 0;
    },
    _alumbra_player_create(config, output) {
      const handle = this.nextHandle++;
      this.handles.add(handle);
      this.HEAPU64[output >>> 3] = handle;
      this.notify = this.callbacks.get(this.HEAPU32[(config >>> 2) + 1]);
      this.notifyToken = this.HEAPU32[(config >>> 2) + 2];
      return 0;
    },
    _alumbra_player_destroy(handle) {
      if (!this.handles.delete(handle)) return 2;
      return 0;
    },
    _alumbra_player_load() { return 0; },
    _alumbra_player_play() { return 0; },
    _alumbra_player_pause() { return 0; },
    _alumbra_player_set_source() { return 0; },
    _alumbra_player_fast_seek() { return 0; },
    _alumbra_player_can_play_type(handle, type, typeSize, output) {
      this.HEAPU32[output >>> 2] = 2;
      return 0;
    },
    _alumbra_player_get_paused(handle, output) { this.HEAPU8[output] = 1; return 0; },
    _alumbra_player_get_ended(handle, output) { this.HEAPU8[output] = 0; return 0; },
    _alumbra_player_get_autoplay(handle, output) { this.HEAPU8[output] = 0; return 0; },
    _alumbra_player_set_autoplay() { return 0; },
    _alumbra_player_get_loop(handle, output) { this.HEAPU8[output] = 0; return 0; },
    _alumbra_player_set_loop() { return 0; },
    _alumbra_player_get_duration(handle, output) { this.HEAPF64[output >>> 3] = 9; return 0; },
    _alumbra_player_get_preload(handle, output) { this.HEAPU32[output >>> 2] = 2; return 0; },
    _alumbra_player_set_preload() { return 0; },
    _alumbra_player_get_default_playback_rate(handle, output) { this.HEAPF64[output >>> 3] = 1; return 0; },
    _alumbra_player_set_default_playback_rate() { return 0; },
    _alumbra_player_get_preserves_pitch(handle, output) { this.HEAPU8[output] = 1; return 0; },
    _alumbra_player_set_preserves_pitch() { return 0; },
    _alumbra_player_get_time_ranges(handle, kind, output) { this.HEAPU32[output >>> 2] = 1; return 0; },
    _alumbra_player_get_time_range(handle, kind, index, start, end) {
      this.HEAPF64[start >>> 3] = 0;
      this.HEAPF64[end >>> 3] = 9;
      return 0;
    },
    _alumbra_player_get_source(handle, output, sizePointer) {
      if (!this.handles.has(handle)) return 2;
      this.HEAPU32[sizePointer >>> 2] = 0;
      return 0;
    },
    _alumbra_player_get_current_time(handle, output) {
      if (!this.handles.has(handle)) return 2;
      this.HEAPF64[output >>> 3] = 2.5;
      return 0;
    },
    _alumbra_player_set_current_time() { return 0; },
    _alumbra_player_get_seeking(handle, output) {
      this.HEAPU8[output] = 1;
      return 0;
    },
    _alumbra_player_get_volume(handle, output) {
      this.HEAPF64[output >>> 3] = 0.75;
      return 0;
    },
    _alumbra_player_set_volume() { return 0; },
    _alumbra_player_get_muted(handle, output) {
      this.HEAPU8[output] = 0;
      return 0;
    },
    _alumbra_player_set_muted() { return 0; },
    _alumbra_player_get_playback_rate(handle, output) {
      this.HEAPF64[output >>> 3] = 1;
      return 0;
    },
    _alumbra_player_set_playback_rate() { return 0; },
    _alumbra_player_get_ready_state(handle, output) {
      this.HEAPU32[output >>> 2] = 3;
      return 0;
    },
    _alumbra_player_get_network_state(handle, output) {
      this.HEAPU32[output >>> 2] = 2;
      return 0;
    },
    _alumbra_player_get_statistics(handle, output) {
      this.HEAPU64[(output + 8) >>> 3] = (1n << 0n) | (1n << 1n) | (1n << 9n);
      this.HEAPF64[(output + 16) >>> 3] = 1.5;
      this.HEAPU64[(output + 24) >>> 3] = 2n;
      this.HEAPF64[(output + 88) >>> 3] = 125;
      return 0;
    },
    _alumbra_player_get_error(handle, info, message, size) {
      this.HEAPU32[(info >>> 2) + 1] = 0;
      this.HEAPU32[size >>> 2] = 0;
      return 0;
    },
    _alumbra_player_poll_event(handle, event, available) {
      if (!this.pendingEvent) {
        this.HEAPU8[available] = 0;
        return 0;
      }
      this.pendingEvent = false;
      this.HEAPU32[(event >>> 2) + 1] = this.eventType;
      const view = new DataView(this.HEAPU8.buffer);
      view.setBigUint64(event + 8, 4n, true);
      view.setFloat64(event + 16, 1.25, true);
      view.setFloat64(event + 24, 9, true);
      this.HEAPU8[available] = 1;
      return 0;
    },
    _alumbra_player_text_track_count(handle, count) {
      this.HEAPU32[count >>> 2] = this.tracks.size;
      return 0;
    },
    _alumbra_player_text_track_at(handle, index, output) {
      const track = [...this.tracks.keys()][index];
      if (track === undefined) return 1;
      this.HEAPU64[output >>> 3] = track;
      return 0;
    },
    _alumbra_player_add_text_track(handle, kind, label, labelSize, language, languageSize, output) {
      const id = this.nextTrack++;
      this.tracks.set(id, {
        kind,
        mode: 0,
        label: this.UTF8ToString(label, labelSize),
        language: this.UTF8ToString(language, languageSize)
      });
      this.HEAPU64[output >>> 3] = id;
      return 0;
    },
    _alumbra_text_track_release(track) { return this.tracks.delete(track) ? 0 : 2; },
    _alumbra_text_track_get_kind(track, output) {
      this.HEAPU32[output >>> 2] = this.tracks.get(track)?.kind ?? 0;
      return this.tracks.has(track) ? 0 : 2;
    },
    _alumbra_text_track_get_mode(track, output) {
      this.HEAPU32[output >>> 2] = this.tracks.get(track)?.mode ?? 0;
      return this.tracks.has(track) ? 0 : 2;
    },
    _alumbra_text_track_set_mode(track, mode) {
      const value = this.tracks.get(track);
      if (value === undefined) return 2;
      value.mode = mode;
      return 0;
    },
    _alumbra_text_track_get_id(track, output, sizePointer) {
      const value = this.tracks.get(track);
      if (value === undefined) return 2;
      this.writeString(value.label.toLowerCase(), output, sizePointer);
      return 0;
    },
    _alumbra_text_track_get_label(track, output, sizePointer) {
      const value = this.tracks.get(track);
      if (value === undefined) return 2;
      this.writeString(value.label, output, sizePointer);
      return 0;
    },
    _alumbra_text_track_get_language(track, output, sizePointer) {
      const value = this.tracks.get(track);
      if (value === undefined) return 2;
      this.writeString(value.language, output, sizePointer);
      return 0;
    },
    _alumbra_settings_get() { return 1; },
    _alumbra_settings_set() { return 0; },
    UTF8ToString(pointer, size) {
      return new TextDecoder().decode(this.HEAPU8.subarray(pointer, pointer + size));
    },
    writeString(value, output, sizePointer) {
      const bytes = new TextEncoder().encode(value);
      const capacity = this.HEAPU32[sizePointer >>> 2];
      this.HEAPU32[sizePointer >>> 2] = bytes.length;
      if (output === 0 || capacity < bytes.length) return bytes.length === 0 ? 0 : 4;
      this.HEAPU8.set(bytes, output);
      return 0;
    },
    stringToUTF8(value, pointer, size) {
      const bytes = new TextEncoder().encode(value);
      this.HEAPU8.set(bytes.subarray(0, size - 1), pointer);
      this.HEAPU8[pointer + Math.min(bytes.length, size - 1)] = 0;
      return Math.min(bytes.length, size - 1);
    },
    lengthBytesUTF8(value) { return new TextEncoder().encode(value).length; }
  };
  return module;
}

test("player bindings expose state and dispose every handle", () => {
  const module = createModule();
  const baseline = module.allocations.size;
  for (let index = 0; index < 500; index += 1) {
    const player = AlumbraPlayer.create(module);
    assert.equal(player.getCurrentTime(), 2.5);
    assert.equal(player.isSeeking(), true);
    assert.equal(player.getVolume(), 0.75);
    assert.equal(player.isMuted(), false);
    assert.equal(player.getPlaybackRate(), 1);
    assert.equal(player.getReadyState(), 3);
    assert.equal(player.getNetworkState(), 2);
    assert.equal(player.currentTime, 2.5);
    assert.equal(player.paused, true);
    assert.equal(player.ended, false);
    assert.equal(player.duration, 9);
    assert.equal(player.canPlayType("video/mp4"), "probably");
    assert.deepEqual(player.buffered, [{ start: 0, end: 9 }]);
    assert.deepEqual(player.seekable, [{ start: 0, end: 9 }]);
    assert.equal(player.preservesPitch, true);
    assert.deepEqual(player.getStatistics(), {
      averageInitialStartupTimeSeconds: 1.5,
      startupFailures: 2n,
      playbackFailurePercentage: null,
      averagePlaybackStalledCount: null,
      averageStalledTimePercentage: null,
      averagePlaybackBitrateKbps: null,
      averageSegmentBitrateBps: null,
      renditionSwitchCount: null,
      droppedFrames: null,
      liveLatencyMs: 125
    });
    player.dispose();
    player.dispose();
  }
  assert.equal(module.handles.size, 0);
  assert.equal(module.allocations.size, baseline);
  assert.equal(module.callbacks.size, 0);
});

test("notify hook pulls events and dispatches them on the JS event loop", async () => {
  const module = createModule();
  const player = AlumbraPlayer.create(module);
  const received = new Promise((resolve) => {
    player.addEventListener("timeupdate", (event) => resolve(event.detail));
  });
  module.pendingEvent = true;
  module.notify(module.notifyToken);
  assert.deepEqual(await received, {
    type: "timeupdate",
    sequence: 4n,
    currentTime: 1.25,
    duration: 9
  });
  player.dispose();
});

test("play promise resolves on the pulled playing event", async () => {
  const module = createModule();
  const player = AlumbraPlayer.create(module);
  const result = player.play();
  module.eventType = 11;
  module.pendingEvent = true;
  module.notify(module.notifyToken);
  await result;
  player.dispose();
});

test("text track handles expose typed metadata and release explicitly", () => {
  const module = createModule();
  const player = AlumbraPlayer.create(module);
  const track = player.addTextTrack(0, "English", "en");
  assert.equal(track.kind, 0);
  assert.equal(track.label, "English");
  assert.equal(track.language, "en");
  track.mode = 2;
  assert.equal(track.mode, 2);
  track.dispose();
  assert.equal(module.tracks.size, 0);
  player.dispose();
});

test("JavaScript MediaError categories match the C++ category table", () => {
  const categories = new Map([
    ["network", 2], ["ads", 2], ["manifest", 4], ["demux", 3],
    ["decode", 3], ["drm", 3], ["text", 3], ["player", 3]
  ]);
  for (const [category, code] of categories) {
    assert.equal(mapErrorCategory(category), code);
    assert.equal(mapMediaErrorCode(code), code);
  }
  assert.throws(() => mapMediaErrorCode(0), RangeError);
});

test("browser media payload transfer counts its single explicit copy", () => {
  const transfer = new MediaPayloadTransfer();
  const source = Uint8Array.from([0, 1, 2, 3]);
  const output = transfer.copyForBrowser(source);
  assert.equal(output, source);
  let appended;
  transfer.appendBuffer({ appendBuffer(value) { appended = value; } }, source);
  assert.equal(appended, source);
  class Chunk {
    constructor(options) { this.options = options; }
  }
  const chunk = transfer.createEncodedVideoChunk(Chunk, { type: "key", timestamp: 0 }, source);
  assert.equal(chunk.options.data, source);
  assert.deepEqual(transfer.stats, { samples: 3, copies: 0, bytesCopied: 0 });
  transfer.reset();
  assert.deepEqual(transfer.stats, { samples: 0, copies: 0, bytesCopied: 0 });
});
