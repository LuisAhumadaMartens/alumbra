import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { createServer } from "node:http";
import { resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { test } from "node:test";

const playwrightModule = process.env.ALUMBRA_PLAYWRIGHT_PATH
  ? await import(pathToFileURL(resolve(process.env.ALUMBRA_PLAYWRIGHT_PATH)))
  : await import("playwright");
const { chromium } = playwrightModule;

const repositoryRoot = resolve(fileURLToPath(new URL("../../..", import.meta.url)));

test("browser payload forwarding preserves the WASM view at media API calls", async () => {
  const source = await readFile(resolve(repositoryRoot, "web/alumbra.mjs"));
  const server = createServer((request, response) => {
    if (request.url !== "/alumbra.mjs") {
      response.writeHead(404).end();
      return;
    }
    response.writeHead(200, { "content-type": "text/javascript" }).end(source);
  });
  await new Promise((resolveListen) => server.listen(0, "127.0.0.1", resolveListen));
  let browser;
  try {
    browser = await chromium.launch({ headless: true });
    const page = await browser.newPage();
    const address = server.address();
    await page.goto(`http://127.0.0.1:${address.port}/`);
    const result = await page.evaluate(async () => {
      const { AlumbraMediaError, MediaPayloadTransfer } = await import("/alumbra.mjs");
      const transfer = new MediaPayloadTransfer();
      const payload = new Uint8Array([1, 2, 3, 4]);
      let appended;
      transfer.appendBuffer({ appendBuffer(value) { appended = value; } }, payload);
      const chunk = transfer.createEncodedVideoChunk(
        class EncodedChunk { constructor(options) { this.options = options; } },
        { type: "key", timestamp: 0 },
        payload
      );
      return {
        sameView: appended === payload && chunk.options.data === payload,
        stats: transfer.stats,
        mediaErrorCodes: [1, 2, 3, 4].map((code) => new AlumbraMediaError(code, "error").code)
      };
    });
    assert.equal(result.sameView, true);
    assert.deepEqual(result.stats, { samples: 2, copies: 0, bytesCopied: 0 });
    assert.deepEqual(result.mediaErrorCodes, [1, 2, 3, 4]);
  } finally {
    if (browser !== undefined) {
      await browser.close();
    }
    server.closeAllConnections();
    await new Promise((resolveClose, reject) => server.close((error) => error ? reject(error) : resolveClose()));
  }
});
