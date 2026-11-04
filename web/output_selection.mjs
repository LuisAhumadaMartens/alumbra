const outputPaths = new Set(["auto", "mse", "webcodecs"]);

const stableValue = (value) => Array.isArray(value)
  ? value.map(stableValue)
  : value !== null && typeof value === "object"
    ? Object.fromEntries(Object.entries(value)
      .sort(([left], [right]) => left < right ? -1 : left > right ? 1 : 0)
      .map(([key, item]) => [key, stableValue(item)]))
    : value;

const stableKey = (value) => JSON.stringify(stableValue(value));

const safeCall = async (callback, fallback) => {
  if (typeof callback !== "function") {
    return fallback;
  }
  try {
    return await callback();
  } catch {
    return fallback;
  }
};

export class CapabilitySession {
  #api;
  #cache = new Map();

  constructor(api = globalThis) {
    this.#api = api;
  }

  async Probe(stream, options = {}) {
    const key = stableKey({ stream, options });
    if (!this.#cache.has(key)) {
      this.#cache.set(key, this.#Probe(stream, options));
    }
    return this.#cache.get(key);
  }

  get cacheSize() {
    return this.#cache.size;
  }

  async #Probe(stream, options) {
    const mediaSource = options.isIphone
      ? this.#api.ManagedMediaSource
      : this.#api.MediaSource ?? this.#api.ManagedMediaSource;
    const mediaSourceName = options.isIphone
      ? "managed_media_source"
      : this.#api.MediaSource
        ? "media_source"
        : "managed_media_source";
    const mimeType = stream.mseMimeType ?? stream.mimeType;
    const mseTypeSupported = typeof mediaSource?.isTypeSupported === "function"
      ? await safeCall(() => mediaSource.isTypeSupported(mimeType), false)
      : false;
    const mediaCapabilities = this.#api.navigator?.mediaCapabilities;
    const decodingConfig = {
      video: stream.type === "video" ? {
        contentType: mimeType,
        width: stream.width,
        height: stream.height,
        bitrate: stream.bitrate,
        framerate: stream.frameRate,
      } : undefined,
      audio: stream.type === "audio" ? {
        contentType: mimeType,
        channels: stream.channels ?? "2",
        bitrate: stream.bitrate,
        samplerate: stream.sampleRate ?? 48000,
      } : undefined,
    };
    const decodingInfo = typeof mediaCapabilities?.decodingInfo === "function"
      ? await safeCall(
        () => mediaCapabilities.decodingInfo({
          type: "media-source",
          ...decodingConfig,
        }),
        { supported: mseTypeSupported, smooth: false, powerEfficient: false },
      )
      : { supported: mseTypeSupported, smooth: false, powerEfficient: false };
    const fileDecodingInfo = typeof mediaCapabilities?.decodingInfo === "function"
      ? await safeCall(
        () => mediaCapabilities.decodingInfo({ type: "file", ...decodingConfig }),
        { supported: false, smooth: false, powerEfficient: false },
      )
      : { supported: false, smooth: false, powerEfficient: false };
    const decoder = stream.type === "audio" ? this.#api.AudioDecoder : this.#api.VideoDecoder;
    const config = stream.type === "audio"
      ? { codec: stream.codec, sampleRate: stream.sampleRate ?? 48000, numberOfChannels: stream.channels ?? 2 }
      : { codec: stream.codec, codedWidth: stream.width, codedHeight: stream.height };
    const decoderResult = typeof decoder?.isConfigSupported === "function"
      ? await safeCall(() => decoder.isConfigSupported(config), { supported: false })
      : { supported: false };
    return Object.freeze({
      mediaSource: mediaSourceName,
      mse: Object.freeze({
        supported: mseTypeSupported && decodingInfo.supported === true,
        smooth: decodingInfo.smooth === true,
        powerEfficient: decodingInfo.powerEfficient === true,
      }),
      webcodecs: Object.freeze({
        supported: decoderResult.supported === true,
        smooth: fileDecodingInfo.smooth === true,
        powerEfficient: fileDecodingInfo.powerEfficient === true,
      }),
    });
  }
}

export class PresentationOutputSelection {
  #capabilities;
  #decision;

  constructor(capabilities = new CapabilitySession()) {
    this.#capabilities = capabilities;
  }

  Select(input, options = {}) {
    if (!this.#decision) {
      this.#decision = SelectOutputPath(input, this.#capabilities, options);
    }
    return this.#decision;
  }
}

const allStreamsSupport = (rendition, path, probes) => rendition.streams.length > 0
  && rendition.streams.every((stream) => {
  const result = probes.get(stream.id);
  return result?.[path].supported === true;
});

const softwareCanDecode = (stream, probe, options) => {
  if (probe.webcodecs.supported || options.addonEnabled !== true || !options.addonUrl) {
    return false;
  }
  if (stream.type === "video" && options.softwareMaxHeight > 0
      && stream.height > options.softwareMaxHeight) {
    return false;
  }
  return true;
};

const decideHardRule = (input, preferredPath, renditions) => {
  if (input.encrypted) {
    return { path: "mse", reason: "encrypted_requires_mse" };
  }
  if (input.hdr) {
    return { path: "mse", reason: "hdr_requires_mse" };
  }
  if (input.isIphone) {
    return { path: "mse", reason: "iphone_managed_media_source" };
  }
  if (input.activeIma && preferredPath !== "webcodecs") {
    return { path: "mse", reason: "ima_requires_mse" };
  }
  if (preferredPath === "mse") {
    return { path: "mse", reason: "integrator_preferred_mse" };
  }
  if (preferredPath === "webcodecs") {
    return { path: "webcodecs", reason: "integrator_preferred_webcodecs" };
  }
  if (renditions.some((rendition) => rendition.mseSupported)) {
    return { path: "mse", reason: "mse_supports_rendition" };
  }
  if (renditions.some((rendition) => rendition.webcodecsSupported)) {
    return { path: "webcodecs", reason: "webcodecs_fallback" };
  }
  if (renditions.some((rendition) => rendition.softwareSupported)) {
    return { path: "webcodecs", reason: "software_decoder_fallback" };
  }
  return { path: "unsupported", reason: "no_supported_output_path" };
};

export async function SelectOutputPath(input, session, options = {}) {
  const preferredPath = outputPaths.has(input.preferredPath) ? input.preferredPath : "auto";
  const streams = input.renditions.flatMap((rendition) => rendition.streams);
  const uniqueStreams = [...new Map(streams.map((stream) => [stream.id, stream])).values()];
  const probes = new Map(await Promise.all(uniqueStreams.map(async (stream) => [
    stream.id,
    await session.Probe(stream, { isIphone: input.isIphone === true }),
  ])));
  const renditions = input.renditions.map((rendition) => ({
    id: rendition.id,
    mseSupported: allStreamsSupport(rendition, "mse", probes),
    webcodecsSupported: allStreamsSupport(rendition, "webcodecs", probes),
    softwareSupported: rendition.streams.length > 0 && rendition.streams.every((stream) => softwareCanDecode(
      stream,
      probes.get(stream.id),
      options,
    )),
  }));
  const decision = decideHardRule(input, preferredPath, renditions);
  const available = decision.path === "mse"
    ? renditions.some((rendition) => rendition.mseSupported)
    : decision.path === "webcodecs"
      ? renditions.some((rendition) => rendition.webcodecsSupported || rendition.softwareSupported)
      : false;
  const warnings = [];
  let path = decision.path;
  let reason = decision.reason;
  if (!available && path !== "unsupported") {
    if (preferredPath !== "auto") {
      warnings.push("preferred_output_path_unavailable");
    }
    const fallback = path === "mse" && !input.encrypted && !input.hdr && !input.isIphone
      && (!input.activeIma || preferredPath === "webcodecs")
      && renditions.some((rendition) => rendition.webcodecsSupported || rendition.softwareSupported);
    if (fallback) {
      path = "webcodecs";
      reason = renditions.some((rendition) => rendition.webcodecsSupported)
        ? "mse_unavailable_webcodecs_fallback"
        : "mse_unavailable_software_decoder_fallback";
    } else if (path === "webcodecs" && renditions.some((rendition) => rendition.mseSupported)) {
      path = "mse";
      reason = "webcodecs_unavailable_mse_fallback";
    } else {
      path = "unsupported";
      reason = "no_supported_output_path";
    }
  }
  if (input.activeIma && preferredPath === "webcodecs") {
    warnings.push("ima_unavailable_on_webcodecs");
  }
  const result = Object.freeze({
    path,
    reason,
    deviceClass: input.deviceClass ?? "unknown",
    mediaSource: probes.values().next().value?.mediaSource
      ?? (input.isIphone ? "managed_media_source" : "media_source"),
    probes,
    renditions: Object.freeze(renditions.map((rendition) => Object.freeze({
      ...rendition,
      eligible: path === "mse" ? rendition.mseSupported
        : path === "webcodecs" ? rendition.webcodecsSupported || rendition.softwareSupported : false,
    }))),
    warnings: Object.freeze(warnings),
    probeCacheSize: session.cacheSize,
    softwareDecode: Object.freeze({
      enabled: options.addonEnabled === true,
      url: options.addonUrl ?? "",
      maxHeight: options.softwareMaxHeight ?? 0,
      required: path === "webcodecs" && renditions.some((rendition) => rendition.softwareSupported),
    }),
  });
  options.log?.({
    event: "web_output_selected",
    path: result.path,
    reason: result.reason,
    warning_codes: result.warnings,
    device_class: result.deviceClass,
    media_source: result.mediaSource,
    probes: Object.fromEntries(result.probes),
    renditions: result.renditions,
  });
  return result;
}
