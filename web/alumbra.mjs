const eventNames = [
  "loadstart", "progress", "suspend", "abort", "error", "emptied", "stalled",
  "loadedmetadata", "loadeddata", "canplay", "canplaythrough", "playing",
  "waiting", "seeking", "seeked", "ended", "durationchange", "timeupdate",
  "play", "pause", "ratechange", "resize", "volumechange", "warning"
];

const livePlayers = new FinalizationRegistry(({ handle, token }) => {
  AlumbraPlayer.instances.delete(token);
  console.warn(`Alumbra player handle ${handle} was not disposed`);
});

export class AlumbraBindingError extends Error {
  constructor(status, operation) {
    super(`Alumbra ${operation} failed with status ${status}`);
    this.name = "AlumbraBindingError";
    this.status = status;
  }
}

export class AlumbraMediaError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "MediaError";
    this.code = mapMediaErrorCode(code);
  }
}

export function mapMediaErrorCode(code) {
  if (Number.isInteger(code) && code >= 1 && code <= 4) {
    return code;
  }
  throw new RangeError(`Unknown MediaError code ${code}`);
}

export function mapErrorCategory(category) {
  const codes = {
    network: 2,
    ads: 2,
    manifest: 4,
    demux: 3,
    decode: 3,
    drm: 3,
    text: 3,
    player: 3
  };
  const code = codes[category];
  if (code === undefined) {
    throw new RangeError(`Unknown Alumbra error category ${category}`);
  }
  return code;
}

function check(module, status, operation) {
  if (status !== 0) {
    throw new AlumbraBindingError(status, operation);
  }
}

function allocate(module, size) {
  const pointer = module._malloc(Math.max(size, 1));
  if (pointer === 0) {
    throw new RangeError("Unable to allocate WASM memory");
  }
  return pointer;
}

function writeString(module, value) {
  const size = module.lengthBytesUTF8(value);
  const pointer = allocate(module, size + 1);
  module.stringToUTF8(value, pointer, size + 1);
  return { pointer, size };
}

function readString(module, handle, functionName, value) {
  const sizePointer = allocate(module, 4);
  const input = value === undefined ? null : writeString(module, value);
  let output = 0;
  try {
    module.HEAPU32[sizePointer >>> 2] = 0;
    const firstStatus = input === null
      ? module[functionName](handle, 0, sizePointer)
      : module[functionName](handle, input.pointer, input.size, 0, sizePointer);
    if (firstStatus !== 0 && firstStatus !== 4) {
      check(module, firstStatus, functionName);
    }
    const size = module.HEAPU32[sizePointer >>> 2];
    output = allocate(module, size + 1);
    module.HEAPU32[sizePointer >>> 2] = size;
    const finalStatus = input === null
      ? module[functionName](handle, output, sizePointer)
      : module[functionName](handle, input.pointer, input.size, output, sizePointer);
    check(module, finalStatus, functionName);
    return module.UTF8ToString(output, size);
  } finally {
    if (input !== null && input !== undefined) {
      module._free(input.pointer);
    }
    if (output !== 0) {
      module._free(output);
    }
    module._free(sizePointer);
  }
}

function readHandleString(module, handle, functionName) {
  const sizePointer = allocate(module, 4);
  let output = 0;
  try {
    module.HEAPU32[sizePointer >>> 2] = 0;
    const status = module[functionName](handle, 0, sizePointer);
    if (status !== 0 && status !== 4) {
      check(module, status, functionName);
    }
    const size = module.HEAPU32[sizePointer >>> 2];
    output = allocate(module, size + 1);
    module.HEAPU32[sizePointer >>> 2] = size;
    check(module, module[functionName](handle, output, sizePointer), functionName);
    return module.UTF8ToString(output, size);
  } finally {
    if (output !== 0) {
      module._free(output);
    }
    module._free(sizePointer);
  }
}

export class AlumbraPlayer extends EventTarget {
  static nextToken = 1;
  static create(module) {
    const version = allocate(module, 12);
    const config = allocate(module, 12);
    const handlePointer = allocate(module, 8);
    let notifyPointer = 0;
    try {
      if (module._alumbra_c_api_version() !== 1) {
        throw new AlumbraBindingError(3, "version negotiation");
      }
      module.HEAPU32[version >>> 2] = 12;
      module.HEAPU32[(version >>> 2) + 1] = 1;
      module.HEAPU32[(version >>> 2) + 2] = 0;
      check(module, module._alumbra_negotiate_version(version), "version negotiation");
      module.HEAPU32[config >>> 2] = 12;
      notifyPointer = module.addFunction((userData) => {
        if (userData !== 0) {
          const player = AlumbraPlayer.instances.get(userData)?.deref();
          player?.scheduleDrain();
        }
      }, "vi");
      module.HEAPU32[(config >>> 2) + 1] = notifyPointer;
      const token = AlumbraPlayer.nextToken++;
      module.HEAPU32[(config >>> 2) + 2] = token;
      module.HEAPU32[handlePointer >>> 2] = 0;
      module.HEAPU32[(handlePointer >>> 2) + 1] = 0;
      check(module, module._alumbra_player_create(config, handlePointer), "player creation");
      const handle = module.HEAPU64[handlePointer >>> 3];
      const player = new AlumbraPlayer(module, handle, notifyPointer, token);
      livePlayers.register(player, { handle, token }, player);
      return player;
    } catch (error) {
      if (notifyPointer !== 0) {
        module.removeFunction(notifyPointer);
      }
      throw error;
    } finally {
      module._free(version);
      module._free(config);
      module._free(handlePointer);
    }
  }

  static instances = new Map();

  constructor(module, handle, notifyPointer, token) {
    super();
    this.module = module;
    this.handle = handle;
    this.notifyPointer = notifyPointer;
    this.token = token;
    this.disposed = false;
    this.drainPending = false;
    this.pendingPlays = [];
    AlumbraPlayer.instances.set(token, new WeakRef(this));
  }

  dispose() {
    if (this.disposed) {
      return;
    }
    check(this.module, this.module._alumbra_player_destroy(this.handle), "player destruction");
    this.cancelPendingPlay("AbortError", "Player was disposed before playback began");
    this.disposed = true;
    AlumbraPlayer.instances.delete(this.token);
    livePlayers.unregister(this);
    this.module.removeFunction(this.notifyPointer);
  }

  load() {
    this.cancelPendingPlay("AbortError", "Playback was interrupted by load()");
    this.call("_alumbra_player_load", "load");
  }
  play() {
    this.ensureLive();
    return new Promise((resolve, reject) => {
      const pending = { resolve, reject };
      this.pendingPlays.push(pending);
      try {
        check(this.module, this.module._alumbra_player_play(this.handle), "play");
      } catch (error) {
        this.pendingPlays = this.pendingPlays.filter((entry) => entry !== pending);
        reject(error);
      }
    });
  }
  pause() {
    this.cancelPendingPlay("AbortError", "Playback was interrupted by pause()");
    this.call("_alumbra_player_pause", "pause");
  }
  get src() { return this.getSource(); }
  set src(value) { this.setSource(value); }
  get currentTime() { return this.getCurrentTime(); }
  set currentTime(value) { this.setCurrentTime(value); }
  get seeking() { return this.isSeeking(); }
  get paused() { return this.isPaused(); }
  get ended() { return this.hasEnded(); }
  get autoplay() { return this.getAutoplay(); }
  set autoplay(value) { this.setAutoplay(value); }
  get loop() { return this.getLoop(); }
  set loop(value) { this.setLoop(value); }
  get duration() { return this.getDuration(); }
  get buffered() { return this.getBuffered(); }
  get seekable() { return this.getSeekable(); }
  get preload() { return this.getPreload(); }
  set preload(value) { this.setPreload(value); }
  get playbackRate() { return this.getPlaybackRate(); }
  set playbackRate(value) { this.setPlaybackRate(value); }
  get defaultPlaybackRate() { return this.getDefaultPlaybackRate(); }
  set defaultPlaybackRate(value) { this.setDefaultPlaybackRate(value); }
  get preservesPitch() { return this.getPreservesPitch(); }
  set preservesPitch(value) { this.setPreservesPitch(value); }
  get volume() { return this.getVolume(); }
  set volume(value) { this.setVolume(value); }
  get muted() { return this.isMuted(); }
  set muted(value) { this.setMuted(value); }
  get readyState() { return this.getReadyState(); }
  get networkState() { return this.getNetworkState(); }
  get error() { return this.getError(); }
  fastSeek(seconds) { this.call("_alumbra_player_fast_seek", "fast seek", seconds); }
  canPlayType(type) {
    this.ensureLive();
    const bytes = writeString(this.module, type);
    const output = allocate(this.module, 4);
    try {
      check(this.module, this.module._alumbra_player_can_play_type(this.handle, bytes.pointer, bytes.size, output), "can play type");
      return ["", "maybe", "probably"][this.module.HEAPU32[output >>> 2]] ?? "";
    } finally {
      this.module._free(bytes.pointer);
      this.module._free(output);
    }
  }
  setSource(source) {
    const bytes = writeString(this.module, source);
    try {
      this.call("_alumbra_player_set_source", "set source", bytes.pointer, bytes.size);
    } finally {
      this.module._free(bytes.pointer);
    }
  }
  getSource() { return readString(this.module, this.handle, "_alumbra_player_get_source"); }
  getCurrentTime() { return this.readNumber("_alumbra_player_get_current_time", "current time"); }
  setCurrentTime(seconds) { this.call("_alumbra_player_set_current_time", "set current time", seconds); }
  isSeeking() { return this.readBoolean("_alumbra_player_get_seeking", "seeking"); }
  isPaused() { return this.readBoolean("_alumbra_player_get_paused", "paused"); }
  hasEnded() { return this.readBoolean("_alumbra_player_get_ended", "ended"); }
  getAutoplay() { return this.readBoolean("_alumbra_player_get_autoplay", "autoplay"); }
  setAutoplay(value) { this.call("_alumbra_player_set_autoplay", "set autoplay", Number(value)); }
  getLoop() { return this.readBoolean("_alumbra_player_get_loop", "loop"); }
  setLoop(value) { this.call("_alumbra_player_set_loop", "set loop", Number(value)); }
  getDuration() { return this.readNumber("_alumbra_player_get_duration", "duration"); }
  getBuffered() { return this.readTimeRanges(0); }
  getSeekable() { return this.readTimeRanges(1); }
  getPreload() { return this.readInteger("_alumbra_player_get_preload", "preload"); }
  setPreload(value) { this.call("_alumbra_player_set_preload", "set preload", value); }
  getVolume() { return this.readNumber("_alumbra_player_get_volume", "volume"); }
  setVolume(volume) { this.call("_alumbra_player_set_volume", "set volume", volume); }
  isMuted() { return this.readBoolean("_alumbra_player_get_muted", "muted"); }
  setMuted(muted) { this.call("_alumbra_player_set_muted", "set muted", Number(muted)); }
  getPlaybackRate() { return this.readNumber("_alumbra_player_get_playback_rate", "playback rate"); }
  setPlaybackRate(rate) { this.call("_alumbra_player_set_playback_rate", "set playback rate", rate); }
  getDefaultPlaybackRate() { return this.readNumber("_alumbra_player_get_default_playback_rate", "default playback rate"); }
  setDefaultPlaybackRate(rate) { this.call("_alumbra_player_set_default_playback_rate", "set default playback rate", rate); }
  getPreservesPitch() { return this.readBoolean("_alumbra_player_get_preserves_pitch", "preserves pitch"); }
  setPreservesPitch(value) { this.call("_alumbra_player_set_preserves_pitch", "set preserves pitch", Number(value)); }
  getReadyState() { return this.readInteger("_alumbra_player_get_ready_state", "ready state"); }
  getNetworkState() { return this.readInteger("_alumbra_player_get_network_state", "network state"); }
  getSetting(key) { return readString(this.module, this.handle, "_alumbra_settings_get", key); }
  setSetting(key, value) {
    const keyBytes = writeString(this.module, key);
    const valueBytes = writeString(this.module, value);
    try {
      check(this.module, this.module._alumbra_settings_set(this.handle, keyBytes.pointer, keyBytes.size, valueBytes.pointer, valueBytes.size), "setting update");
    } finally {
      this.module._free(keyBytes.pointer);
      this.module._free(valueBytes.pointer);
    }
  }

  getError() {
    this.ensureLive();
    const info = allocate(this.module, 12);
    const sizePointer = allocate(this.module, 4);
    let message = 0;
    try {
      this.module.HEAPU32[info >>> 2] = 12;
      this.module.HEAPU32[sizePointer >>> 2] = 0;
      const status = this.module._alumbra_player_get_error(this.handle, info, 0, sizePointer);
      if (status !== 0 && status !== 4) {
        check(this.module, status, "read error");
      }
      const code = this.module.HEAPU32[(info >>> 2) + 1];
      if (code === 0) {
        return null;
      }
      const size = this.module.HEAPU32[sizePointer >>> 2];
      message = allocate(this.module, size + 1);
      this.module.HEAPU32[sizePointer >>> 2] = size;
      check(this.module, this.module._alumbra_player_get_error(this.handle, info, message, sizePointer), "read error");
      return new AlumbraMediaError(code, this.module.UTF8ToString(message, size));
    } finally {
      if (message !== 0) {
        this.module._free(message);
      }
      this.module._free(info);
      this.module._free(sizePointer);
    }
  }

  getStatistics() {
    this.ensureLive();
    const pointer = allocate(this.module, 96);
    try {
      this.module.HEAPU32[pointer >>> 2] = 96;
      check(this.module, this.module._alumbra_player_get_statistics(this.handle, pointer), "read statistics");
      const mask = this.module.HEAPU64[(pointer + 8) >>> 3];
      const doubleValue = (bit, offset) =>
        (mask & (1n << BigInt(bit))) === 0n
          ? null
          : this.module.HEAPF64[(pointer + offset) >>> 3];
      const integerValue = (bit, offset) =>
        (mask & (1n << BigInt(bit))) === 0n
          ? null
          : this.module.HEAPU64[(pointer + offset) >>> 3];
      return Object.freeze({
        averageInitialStartupTimeSeconds: doubleValue(0, 16),
        startupFailures: integerValue(1, 24),
        playbackFailurePercentage: doubleValue(2, 32),
        averagePlaybackStalledCount: doubleValue(3, 40),
        averageStalledTimePercentage: doubleValue(4, 48),
        averagePlaybackBitrateKbps: doubleValue(5, 56),
        averageSegmentBitrateBps: doubleValue(6, 64),
        renditionSwitchCount: integerValue(7, 72),
        droppedFrames: integerValue(8, 80),
        liveLatencyMs: doubleValue(9, 88)
      });
    } finally {
      this.module._free(pointer);
    }
  }

  getTextTracks() {
    this.ensureLive();
    const countPointer = allocate(this.module, 4);
    const trackPointer = allocate(this.module, 8);
    const tracks = [];
    try {
      check(this.module, this.module._alumbra_player_text_track_count(this.handle, countPointer), "text track count");
      const count = this.module.HEAPU32[countPointer >>> 2];
      for (let index = 0; index < count; index += 1) {
        check(this.module, this.module._alumbra_player_text_track_at(this.handle, index, trackPointer), "text track lookup");
        tracks.push(new AlumbraTextTrack(this.module, this.module.HEAPU64[trackPointer >>> 3]));
      }
      return tracks;
    } finally {
      this.module._free(countPointer);
      this.module._free(trackPointer);
    }
  }

  addTextTrack(kind, label, language) {
    this.ensureLive();
    const labelBytes = writeString(this.module, label);
    const languageBytes = writeString(this.module, language);
    const trackPointer = allocate(this.module, 8);
    try {
      check(this.module, this.module._alumbra_player_add_text_track(
        this.handle, kind, labelBytes.pointer, labelBytes.size,
        languageBytes.pointer, languageBytes.size, trackPointer), "add text track");
      return new AlumbraTextTrack(this.module, this.module.HEAPU64[trackPointer >>> 3]);
    } finally {
      this.module._free(labelBytes.pointer);
      this.module._free(languageBytes.pointer);
      this.module._free(trackPointer);
    }
  }

  call(name, operation, ...args) {
    this.ensureLive();
    check(this.module, this.module[name](this.handle, ...args), operation);
  }

  readNumber(name, operation) {
    this.ensureLive();
    const output = allocate(this.module, 8);
    try {
      check(this.module, this.module[name](this.handle, output), operation);
      return this.module.HEAPF64[output >>> 3];
    } finally {
      this.module._free(output);
    }
  }

  readBoolean(name, operation) {
    this.ensureLive();
    const output = allocate(this.module, 4);
    try {
      check(this.module, this.module[name](this.handle, output), operation);
      return this.module.HEAPU8[output] !== 0;
    } finally {
      this.module._free(output);
    }
  }

  readInteger(name, operation) {
    this.ensureLive();
    const output = allocate(this.module, 4);
    try {
      check(this.module, this.module[name](this.handle, output), operation);
      return this.module.HEAPU32[output >>> 2];
    } finally {
      this.module._free(output);
    }
  }

  readTimeRanges(kind) {
    this.ensureLive();
    const countPointer = allocate(this.module, 4);
    const startPointer = allocate(this.module, 8);
    const endPointer = allocate(this.module, 8);
    try {
      check(this.module, this.module._alumbra_player_get_time_ranges(this.handle, kind, countPointer), "time range count");
      const count = this.module.HEAPU32[countPointer >>> 2];
      const result = [];
      for (let index = 0; index < count; index += 1) {
        check(this.module, this.module._alumbra_player_get_time_range(this.handle, kind, index, startPointer, endPointer), "time range lookup");
        result.push(Object.freeze({
          start: this.module.HEAPF64[startPointer >>> 3],
          end: this.module.HEAPF64[endPointer >>> 3]
        }));
      }
      return Object.freeze(result);
    } finally {
      this.module._free(countPointer);
      this.module._free(startPointer);
      this.module._free(endPointer);
    }
  }

  ensureLive() {
    if (this.disposed) {
      throw new AlumbraBindingError(2, "disposed player access");
    }
  }

  cancelPendingPlay(name, message) {
    const error = new DOMException(message, name);
    for (const pending of this.pendingPlays) {
      pending.reject(error);
    }
    this.pendingPlays = [];
  }

  scheduleDrain() {
    if (this.disposed || this.drainPending) {
      return;
    }
    this.drainPending = true;
    queueMicrotask(() => {
      this.drainPending = false;
      if (!this.disposed) {
        this.drainEvents();
      }
    });
  }

  drainEvents() {
    const eventPointer = allocate(this.module, 40);
    const availablePointer = allocate(this.module, 4);
    try {
      for (;;) {
        this.module.HEAPU32[eventPointer >>> 2] = 40;
        check(this.module, this.module._alumbra_player_poll_event(this.handle, eventPointer, availablePointer), "event polling");
        if (this.module.HEAPU8[availablePointer] === 0) {
          break;
        }
        const view = new DataView(this.module.HEAPU8.buffer, eventPointer, 40);
        const type = view.getUint32(4, true);
        const data = {
          type: eventNames[type] ?? "unknown",
          sequence: view.getBigUint64(8, true),
          currentTime: view.getFloat64(16, true),
          duration: view.getFloat64(24, true)
        };
        const errorCode = view.getUint32(32, true);
        if (errorCode !== 0) {
          data.error = data.type === "error"
            ? this.getError() ?? new AlumbraMediaError(errorCode, "Media playback failed")
            : new AlumbraMediaError(errorCode, "Media playback warning");
        }
        if (data.type === "playing") {
          for (const pending of this.pendingPlays) {
            pending.resolve();
          }
          this.pendingPlays = [];
        } else if (data.type === "error" || data.type === "abort") {
          const error = data.type === "error"
            ? data.error ?? this.getError() ?? new AlumbraMediaError(3, "Media playback failed")
            : new DOMException("Playback was interrupted", "AbortError");
          for (const pending of this.pendingPlays) {
            pending.reject(error);
          }
          this.pendingPlays = [];
        }
        this.dispatchEvent(new CustomEvent(data.type, { detail: data }));
      }
    } finally {
      this.module._free(eventPointer);
      this.module._free(availablePointer);
    }
  }
}

export class AlumbraTextTrack {
  constructor(module, handle) {
    this.module = module;
    this.handle = handle;
    this.disposed = false;
  }

  dispose() {
    if (this.disposed) {
      return;
    }
    check(this.module, this.module._alumbra_text_track_release(this.handle), "text track release");
    this.disposed = true;
  }

  get kind() {
    return this.readNumber("_alumbra_text_track_get_kind", "text track kind");
  }

  get mode() {
    return this.readNumber("_alumbra_text_track_get_mode", "text track mode");
  }

  set mode(value) {
    this.ensureLive();
    check(this.module, this.module._alumbra_text_track_set_mode(this.handle, value), "set text track mode");
  }

  get id() { return this.readString("_alumbra_text_track_get_id"); }
  get label() { return this.readString("_alumbra_text_track_get_label"); }
  get language() { return this.readString("_alumbra_text_track_get_language"); }

  readNumber(name, operation) {
    this.ensureLive();
    const pointer = allocate(this.module, 4);
    try {
      check(this.module, this.module[name](this.handle, pointer), operation);
      return this.module.HEAPU32[pointer >>> 2];
    } finally {
      this.module._free(pointer);
    }
  }

  readString(name) {
    this.ensureLive();
    return readHandleString(this.module, this.handle, name);
  }

  ensureLive() {
    if (this.disposed) {
      throw new AlumbraBindingError(2, "disposed text track access");
    }
  }
}

export class MediaPayloadTransfer {
  constructor() {
    this.samples = 0;
    this.copies = 0;
    this.bytesCopied = 0;
  }

  copyForBrowser(bytes) {
    if (!(bytes instanceof Uint8Array)) {
      throw new TypeError("Media payload must be a Uint8Array");
    }
    this.samples += 1;
    return bytes;
  }

  appendBuffer(sourceBuffer, bytes) {
    sourceBuffer.appendBuffer(this.copyForBrowser(bytes));
  }

  createEncodedVideoChunk(EncodedVideoChunkType, init, bytes) {
    return new EncodedVideoChunkType({ ...init, data: this.copyForBrowser(bytes) });
  }

  get stats() {
    return Object.freeze({ samples: this.samples, copies: this.copies, bytesCopied: this.bytesCopied });
  }

  reset() {
    this.samples = 0;
    this.copies = 0;
    this.bytesCopied = 0;
  }
}
