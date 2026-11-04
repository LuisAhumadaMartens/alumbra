import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { after, before, test } from "node:test";
import { chromium } from "playwright";

const loader = new URL("../../../web/wasm-loader.mjs", import.meta.url);
const source = await readFile(loader, "utf8");
let browser;
let server;
let origin;

before(async () => {
  browser = await chromium.launch({ headless: true });
  server = createServer((request, response) => {
    if (request.url === "/loader.mjs") {
      response.writeHead(200, { "content-type": "text/javascript" });
      response.end(source);
      return;
    }
    const isolated = request.url === "/isolated";
    const headers = { "content-type": "text/html" };
    if (isolated) {
      headers["cross-origin-opener-policy"] = "same-origin";
      headers["cross-origin-embedder-policy"] = "require-corp";
    }
    response.writeHead(200, headers);
    response.end('<script type="module">import { selectWasmVariant } from "/loader.mjs"; window.result = selectWasmVariant();</script>');
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  origin = `http://127.0.0.1:${server.address().port}`;
});

after(async () => {
  await browser?.close();
  if (server) {
    await new Promise((resolve) => server.close(resolve));
  }
});

test("selects the variant allowed by page isolation", async () => {
  const plainPage = await browser.newPage();
  await plainPage.goto(`${origin}/plain`);
  await plainPage.waitForFunction(() => window.result !== undefined);
  const single = await plainPage.evaluate(() => window.result);
  assert.equal(single.threaded, false);
  assert.equal(single.asset, "alumbra-single.js");
  await plainPage.close();
  const isolatedPage = await browser.newPage();
  await isolatedPage.goto(`${origin}/isolated`);
  await isolatedPage.waitForFunction(() => window.result !== undefined);
  const threaded = await isolatedPage.evaluate(() => window.result);
  assert.equal(threaded.threaded, true);
  assert.equal(threaded.asset, "alumbra-threaded.js");
  await isolatedPage.close();
});

