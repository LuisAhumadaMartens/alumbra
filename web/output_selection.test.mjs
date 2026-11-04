import assert from "node:assert/strict";
import test from "node:test";

import { CapabilitySession, PresentationOutputSelection, SelectOutputPath } from "./output_selection.mjs";

const stream = (id, type, codec = "avc1.640028", extra = {}) => ({
  id,
  type,
  codec,
  mimeType: `${type === "video" ? "video/mp4" : "audio/mp4"}; codecs="${codec}"`,
  width: 1920,
  height: 1080,
  frameRate: 120,
  bitrate: 5000000,
  ...extra,
});

const browser = ({ mse = true, video = true, audio = true, mediaCapabilities = true } = {}) => ({
  MediaSource: { isTypeSupported: () => mse },
  ManagedMediaSource: { isTypeSupported: () => mse },
  VideoDecoder: { isConfigSupported: async () => ({ supported: video }) },
  AudioDecoder: { isConfigSupported: async () => ({ supported: audio }) },
  navigator: mediaCapabilities ? {
    mediaCapabilities: {
      decodingInfo: async ({ video: videoConfig, audio: audioConfig }) => ({
        supported: true,
        smooth: videoConfig?.framerate === 120,
        powerEfficient: Boolean(audioConfig || videoConfig),
      }),
    },
  } : {},
});

const presentation = (renditions, overrides = {}) => ({
  preferredPath: "auto",
  renditions,
  ...overrides,
});

const rendition = (id, ...streams) => ({ id, streams });

test("automatic selection prefers MSE when it supports a rendition", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))]),
    new CapabilitySession(browser()),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.reason, "mse_supports_rendition");
  assert.equal(result.renditions[0].eligible, true);
});

test("supported explicit preferences select their requested path", async () => {
  const streams = [rendition("high", stream("v", "video"))];
  const session = new CapabilitySession(browser());
  const mse = await SelectOutputPath(
    presentation(streams, { preferredPath: "mse" }),
    session,
  );
  const webcodecs = await SelectOutputPath(
    presentation(streams, { preferredPath: "webcodecs" }),
    session,
  );
  assert.equal(mse.path, "mse");
  assert.equal(mse.reason, "integrator_preferred_mse");
  assert.equal(webcodecs.path, "webcodecs");
  assert.equal(webcodecs.reason, "integrator_preferred_webcodecs");
});

test("automatic selection falls back to WebCodecs for an MSE container gap", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video", "av01.0.08M.08", {
      mimeType: "video/webm; codecs=av01.0.08M.08",
      mseMimeType: "video/mp4; codecs=av01.0.08M.08",
    }))]),
    new CapabilitySession(browser({ mse: false })),
  );
  assert.equal(result.path, "webcodecs");
  assert.equal(result.reason, "webcodecs_fallback");
});

test("encrypted content always uses MSE", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { encrypted: true }),
    new CapabilitySession(browser()),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.reason, "encrypted_requires_mse");
});

test("HDR content always uses MSE", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { hdr: true }),
    new CapabilitySession(browser()),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.reason, "hdr_requires_mse");
});

test("iPhone uses Managed Media Source", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { isIphone: true }),
    new CapabilitySession(browser()),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.reason, "iphone_managed_media_source");
  assert.equal(result.mediaSource, "managed_media_source");
  assert.equal(result.probes.get("v").mediaSource, "managed_media_source");
});

test("Managed Media Source is probed when Media Source is missing", async () => {
  const api = browser();
  delete api.MediaSource;
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))]),
    new CapabilitySession(api),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.mediaSource, "managed_media_source");
});

test("encrypted, HDR and iPhone rules take precedence over a WebCodecs preference", async () => {
  const session = new CapabilitySession(browser());
  for (const constraints of [
    { encrypted: true, reason: "encrypted_requires_mse" },
    { hdr: true, reason: "hdr_requires_mse" },
    { isIphone: true, reason: "iphone_managed_media_source" },
  ]) {
    const result = await SelectOutputPath(
      presentation([rendition("high", stream("v", "video"))], {
        preferredPath: "webcodecs",
        ...constraints,
      }),
      session,
    );
    assert.equal(result.path, "mse");
    assert.equal(result.reason, constraints.reason);
  }
});

test("IMA requires MSE unless the integrator explicitly selects WebCodecs", async () => {
  const session = new CapabilitySession(browser());
  const forcedMse = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { activeIma: true }),
    session,
  );
  const overridden = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], {
      activeIma: true,
      preferredPath: "webcodecs",
    }),
    session,
  );
  assert.equal(forcedMse.reason, "ima_requires_mse");
  assert.equal(overridden.path, "webcodecs");
  assert.ok(overridden.warnings.includes("ima_unavailable_on_webcodecs"));
});

test("automatic selection does not leave MSE for WebCodecs when IMA requires MSE", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { activeIma: true }),
    new CapabilitySession(browser({ mse: false })),
  );
  assert.equal(result.path, "unsupported");
  assert.equal(result.reason, "no_supported_output_path");
});

test("an unsupported WebCodecs override falls back to MSE with a warning", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { preferredPath: "webcodecs" }),
    new CapabilitySession(browser({ video: false })),
  );
  assert.equal(result.path, "mse");
  assert.equal(result.reason, "webcodecs_unavailable_mse_fallback");
  assert.ok(result.warnings.includes("preferred_output_path_unavailable"));
});

test("an unsupported MSE override falls back to WebCodecs with a warning", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))], { preferredPath: "mse" }),
    new CapabilitySession(browser({ mse: false })),
  );
  assert.equal(result.path, "webcodecs");
  assert.equal(result.reason, "mse_unavailable_webcodecs_fallback");
  assert.ok(result.warnings.includes("preferred_output_path_unavailable"));
});

test("MSE choice filters unsupported renditions before ABR", async () => {
  const result = await SelectOutputPath(
    presentation([
      rendition("h264", stream("h264", "video", "avc1.640028")),
      rendition("av1", stream("av1", "video", "av01.0.08M.08")),
    ]),
    new CapabilitySession({
      ...browser(),
      MediaSource: { isTypeSupported: (type) => type.includes("avc1") },
    }),
  );
  assert.equal(result.path, "mse");
  assert.deepEqual(result.renditions.map(({ id, eligible }) => [id, eligible]), [
    ["h264", true],
    ["av1", false],
  ]);
});

test("audio and video must both be supported on the selected path", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("av-gap", stream("v", "video"), stream("a", "audio", "ac-3"))]),
    new CapabilitySession(browser({ mse: false, audio: false })),
  );
  assert.equal(result.path, "unsupported");
  assert.equal(result.renditions[0].eligible, false);
});

test("capabilities are cached for a session and preserve the real frame rate", async () => {
  let calls = 0;
  let queriedFrameRate;
  const api = browser();
  api.navigator.mediaCapabilities.decodingInfo = async ({ video }) => {
    calls += 1;
    queriedFrameRate = video.framerate;
    return { supported: true, smooth: true, powerEfficient: true };
  };
  const session = new CapabilitySession(api);
  const item = stream("v", "video");
  await session.Probe(item);
  await session.Probe(item);
  assert.equal(calls, 2);
  assert.equal(queriedFrameRate, 120);
  assert.equal(session.cacheSize, 1);
});

test("decision logs include the probe results and rendition eligibility", async () => {
  let record;
  const result = await SelectOutputPath(
    presentation([rendition("h264", stream("v", "video"))], { deviceClass: "desktop" }),
    new CapabilitySession(browser()),
    { log: (value) => { record = value; } },
  );
  assert.equal(record.path, "mse");
  assert.equal(record.reason, "mse_supports_rendition");
  assert.equal(record.device_class, "desktop");
  assert.equal(record.probes.v.mse.supported, true);
  assert.equal(record.renditions[0].eligible, result.renditions[0].eligible);
});

test("a presentation keeps its selected path until a new selection object is created", async () => {
  const selection = new PresentationOutputSelection(new CapabilitySession(browser()));
  const first = await selection.Select(presentation([
    rendition("h264", stream("v1", "video")),
  ]));
  const repeated = await selection.Select(presentation([
    rendition("av1", stream("v2", "video", "av01.0.08M.08")),
  ], { preferredPath: "webcodecs" }));
  assert.equal(first.path, "mse");
  assert.equal(repeated.path, "mse");
  assert.equal(repeated, first);
});

test("software add-on configuration is exposed for the deferred decoder layer", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))]),
    new CapabilitySession(browser({ mse: false, video: false })),
    { addonEnabled: true, addonUrl: "https://example.test/addon.wasm", softwareMaxHeight: 0 },
  );
  assert.equal(result.path, "webcodecs");
  assert.equal(result.reason, "software_decoder_fallback");
  assert.equal(result.renditions[0].eligible, true);
  assert.deepEqual(result.softwareDecode, {
    enabled: true,
    url: "https://example.test/addon.wasm",
    maxHeight: 0,
    required: true,
  });
});

test("software add-on respects its configured maximum video height", async () => {
  const result = await SelectOutputPath(
    presentation([rendition("high", stream("v", "video"))]),
    new CapabilitySession(browser({ mse: false, video: false })),
    { addonEnabled: true, addonUrl: "https://example.test/addon.wasm", softwareMaxHeight: 720 },
  );
  assert.equal(result.path, "unsupported");
  assert.equal(result.renditions[0].eligible, false);
  assert.equal(result.softwareDecode.required, false);
});
