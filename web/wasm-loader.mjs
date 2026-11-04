export function selectWasmVariant({
  crossOriginIsolated = typeof globalThis !== "undefined"
    && globalThis.crossOriginIsolated === true,
  sharedArrayBuffer = typeof globalThis !== "undefined"
    && typeof globalThis.SharedArrayBuffer === "function",
  threading = "auto",
  useWorker = true,
}) {
  const threadSupport = crossOriginIsolated && sharedArrayBuffer;
  const threaded = threading === "threaded"
    ? threadSupport
    : threading === "single"
      ? false
      : threadSupport;
  return {
    asset: threaded ? "alumbra-threaded.js" : "alumbra-single.js",
    threaded,
    useWorker,
  };
}

export async function loadWasm({
  artifacts,
  settings = {},
  instantiate,
  environment = {},
}) {
  const variant = selectWasmVariant({
    ...environment,
    threading: settings.threading ?? "auto",
    useWorker: settings.useWorker ?? true,
  });
  return instantiate(artifacts[variant.asset], variant);
}
