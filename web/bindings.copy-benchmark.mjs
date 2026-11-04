import assert from "node:assert/strict";

import { MediaPayloadTransfer } from "./alumbra.mjs";

const sample = new Uint8Array(256 * 1024);
const transfer = new MediaPayloadTransfer();
const sourceBuffer = { appendBuffer(bytes) { assert.equal(bytes, sample); } };
const iterations = 10000;

for (let index = 0; index < iterations; index += 1) {
  transfer.appendBuffer(sourceBuffer, sample);
}

const stats = transfer.stats;
assert.equal(stats.samples, iterations);
assert.equal(stats.copies, 0);
assert.equal(stats.bytesCopied, 0);
process.stdout.write(JSON.stringify({ ...stats, copiesPerSample: stats.copies / stats.samples }) + "\n");
