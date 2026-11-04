export type AlumbraStatus = 0 | 1 | 2 | 3 | 4 | 5;
export type AlumbraTextTrackKind = 0 | 1 | 2 | 3 | 4;
export type AlumbraTextTrackMode = 0 | 1 | 2;
export type AlumbraPreload = 0 | 1 | 2;

export interface AlumbraTimeRange {
  readonly start: number;
  readonly end: number;
}

export interface AlumbraStatistics {
  readonly averageInitialStartupTimeSeconds: number | null;
  readonly startupFailures: bigint | null;
  readonly playbackFailurePercentage: number | null;
  readonly averagePlaybackStalledCount: number | null;
  readonly averageStalledTimePercentage: number | null;
  readonly averagePlaybackBitrateKbps: number | null;
  readonly averageSegmentBitrateBps: number | null;
  readonly renditionSwitchCount: bigint | null;
  readonly droppedFrames: bigint | null;
  readonly liveLatencyMs: number | null;
}

export interface AlumbraEventData {
  readonly type: string;
  readonly sequence: bigint;
  readonly currentTime: number;
  readonly duration: number;
  readonly error?: AlumbraMediaError;
}

export interface AlumbraModule {
  readonly HEAPU8: Uint8Array;
  readonly HEAPU32: Uint32Array;
  readonly HEAPF64: Float64Array;
  readonly HEAPU64: BigUint64Array;
  readonly _malloc: (size: number) => number;
  readonly _free: (pointer: number) => void;
  readonly addFunction: (callback: (userData: number) => void, signature: string) => number;
  readonly removeFunction: (pointer: number) => void;
  readonly _alumbra_c_api_version: () => number;
  readonly _alumbra_negotiate_version: (info: number) => number;
  readonly _alumbra_player_create: (config: number, handle: number) => number;
  readonly _alumbra_player_destroy: (handle: bigint) => number;
  readonly _alumbra_player_get_source: (handle: bigint, source: number, size: number) => number;
  readonly _alumbra_player_set_source: (handle: bigint, source: number, size: number) => number;
  readonly _alumbra_player_load: (handle: bigint) => number;
  readonly _alumbra_player_play: (handle: bigint) => number;
  readonly _alumbra_player_pause: (handle: bigint) => number;
  readonly _alumbra_player_fast_seek: (handle: bigint, seconds: number) => number;
  readonly _alumbra_player_can_play_type: (handle: bigint, type: number, typeSize: number, result: number) => number;
  readonly _alumbra_player_get_paused: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_ended: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_autoplay: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_autoplay: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_loop: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_loop: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_duration: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_preload: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_preload: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_default_playback_rate: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_default_playback_rate: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_preserves_pitch: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_preserves_pitch: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_time_ranges: (handle: bigint, kind: number, count: number) => number;
  readonly _alumbra_player_get_time_range: (handle: bigint, kind: number, index: number, start: number, end: number) => number;
  readonly _alumbra_player_get_current_time: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_current_time: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_seeking: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_volume: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_volume: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_muted: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_muted: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_playback_rate: (handle: bigint, value: number) => number;
  readonly _alumbra_player_set_playback_rate: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_ready_state: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_network_state: (handle: bigint, value: number) => number;
  readonly _alumbra_player_get_statistics: (handle: bigint, statistics: number) => number;
  readonly _alumbra_player_get_error: (handle: bigint, info: number, message: number, size: number) => number;
  readonly _alumbra_player_poll_event: (handle: bigint, event: number, available: number) => number;
  readonly _alumbra_settings_get: (handle: bigint, key: number, keySize: number, value: number, valueSize: number) => number;
  readonly _alumbra_settings_set: (handle: bigint, key: number, keySize: number, value: number, valueSize: number) => number;
  readonly _alumbra_player_text_track_count: (handle: bigint, count: number) => number;
  readonly _alumbra_player_text_track_at: (handle: bigint, index: number, track: number) => number;
  readonly _alumbra_player_add_text_track: (handle: bigint, kind: number, label: number, labelSize: number, language: number, languageSize: number, track: number) => number;
  readonly _alumbra_text_track_release: (track: bigint) => number;
  readonly _alumbra_text_track_get_kind: (track: bigint, kind: number) => number;
  readonly _alumbra_text_track_get_mode: (track: bigint, mode: number) => number;
  readonly _alumbra_text_track_set_mode: (track: bigint, mode: number) => number;
  readonly _alumbra_text_track_get_id: (track: bigint, id: number, size: number) => number;
  readonly _alumbra_text_track_get_label: (track: bigint, label: number, size: number) => number;
  readonly _alumbra_text_track_get_language: (track: bigint, language: number, size: number) => number;
  readonly UTF8ToString: (pointer: number, maxBytesToRead?: number) => string;
  readonly stringToUTF8: (value: string, pointer: number, maxBytesToWrite: number) => number;
  readonly lengthBytesUTF8: (value: string) => number;
}

export class AlumbraBindingError extends Error {
  readonly status: AlumbraStatus;
}

export class AlumbraMediaError extends Error {
  readonly code: 1 | 2 | 3 | 4;
  readonly name: "MediaError";
}

export class AlumbraTextTrack {
  private constructor(module: AlumbraModule, handle: bigint);
  private readonly module: AlumbraModule;
  private readonly handle: bigint;
  dispose(): void;
  readonly kind: AlumbraTextTrackKind;
  mode: AlumbraTextTrackMode;
  readonly id: string;
  readonly label: string;
  readonly language: string;
}

export class AlumbraPlayer extends EventTarget {
  private constructor(module: AlumbraModule, handle: bigint, notifyPointer: number);
  private readonly module: AlumbraModule;
  private readonly handle: bigint;
  static create(module: AlumbraModule): AlumbraPlayer;
  get src(): string;
  set src(source: string);
  get currentTime(): number;
  set currentTime(seconds: number);
  get seeking(): boolean;
  get paused(): boolean;
  get ended(): boolean;
  get autoplay(): boolean;
  set autoplay(value: boolean);
  get loop(): boolean;
  set loop(value: boolean);
  get duration(): number;
  get buffered(): readonly AlumbraTimeRange[];
  get seekable(): readonly AlumbraTimeRange[];
  get preload(): AlumbraPreload;
  set preload(value: AlumbraPreload);
  get playbackRate(): number;
  set playbackRate(rate: number);
  get defaultPlaybackRate(): number;
  set defaultPlaybackRate(rate: number);
  get preservesPitch(): boolean;
  set preservesPitch(value: boolean);
  get volume(): number;
  set volume(value: number);
  get muted(): boolean;
  set muted(value: boolean);
  get readyState(): number;
  get networkState(): number;
  get error(): AlumbraMediaError | null;
  dispose(): void;
  load(): void;
  play(): Promise<void>;
  pause(): void;
  fastSeek(seconds: number): void;
  canPlayType(type: string): 0 | 1 | 2;
  setSource(source: string): void;
  getSource(): string;
  getCurrentTime(): number;
  setCurrentTime(seconds: number): void;
  isSeeking(): boolean;
  isPaused(): boolean;
  hasEnded(): boolean;
  getAutoplay(): boolean;
  setAutoplay(autoplay: boolean): void;
  getLoop(): boolean;
  setLoop(loop: boolean): void;
  getDuration(): number;
  getBuffered(): readonly AlumbraTimeRange[];
  getSeekable(): readonly AlumbraTimeRange[];
  getPreload(): AlumbraPreload;
  setPreload(preload: AlumbraPreload): void;
  getVolume(): number;
  setVolume(volume: number): void;
  isMuted(): boolean;
  setMuted(muted: boolean): void;
  getPlaybackRate(): number;
  setPlaybackRate(rate: number): void;
  getDefaultPlaybackRate(): number;
  setDefaultPlaybackRate(rate: number): void;
  getPreservesPitch(): boolean;
  setPreservesPitch(preservesPitch: boolean): void;
  getReadyState(): number;
  getNetworkState(): number;
  getError(): AlumbraMediaError | null;
  getStatistics(): AlumbraStatistics;
  getTextTracks(): AlumbraTextTrack[];
  addTextTrack(kind: AlumbraTextTrackKind, label: string, language: string): AlumbraTextTrack;
  getSetting(key: string): string;
  setSetting(key: string, value: string): void;
}

export interface MediaPayloadCopyStats {
  readonly samples: number;
  readonly copies: number;
  readonly bytesCopied: number;
}

export interface AlumbraEncodedVideoChunkOptions {
  readonly type: "key" | "delta";
  readonly timestamp: number;
  readonly duration?: number;
}

export class MediaPayloadTransfer {
  appendBuffer(sourceBuffer: SourceBuffer, bytes: Uint8Array): void;
  copyForBrowser(bytes: Uint8Array): Uint8Array;
  createEncodedVideoChunk(EncodedVideoChunkType: new (options: AlumbraEncodedVideoChunkOptions & { readonly data: Uint8Array }) => unknown, init: AlumbraEncodedVideoChunkOptions, bytes: Uint8Array): unknown;
  readonly stats: MediaPayloadCopyStats;
  reset(): void;
}

export function mapMediaErrorCode(code: number): 1 | 2 | 3 | 4;
export function mapErrorCategory(category: string): 2 | 3 | 4;
