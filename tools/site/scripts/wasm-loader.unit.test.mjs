import assert from "node:assert/strict";
import { test } from "node:test";
import { loadWasm, selectWasmVariant } from "../../../web/wasm-loader.mjs";

test("honours explicit single mode and worker preference", () => {
  assert.deepEqual(selectWasmVariant({
    threading: "single",
    useWorker: false,
    crossOriginIsolated: true,
    sharedArrayBuffer: true,
  }), {
    asset: "alumbra-single.js",
    threaded: false,
    useWorker: false,
  });
});

test("falls back when threaded mode lacks isolation support", () => {
  assert.deepEqual(selectWasmVariant({
    threading: "threaded",
    crossOriginIsolated: false,
    sharedArrayBuffer: true,
  }), {
    asset: "alumbra-single.js",
    threaded: false,
    useWorker: true,
  });
});

test("loads only the selected artifact", async () => {
  const seen = [];
  const result = await loadWasm({
    artifacts: {
      "alumbra-single.js": "single",
      "alumbra-threaded.js": "threaded",
    },
    settings: { threading: "auto", useWorker: false },
    environment: { crossOriginIsolated: false, sharedArrayBuffer: false },
    instantiate: async (artifact, variant) => {
      seen.push(artifact);
      return { artifact, variant };
    },
  });
  assert.deepEqual(seen, ["single"]);
  assert.equal(result.variant.useWorker, false);
});
