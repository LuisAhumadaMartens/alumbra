import assert from "node:assert/strict";
import test from "node:test";

import "./fetch_transport.mjs";

const { createFetchTransport } = globalThis.AlumbraFetchTransport;

function recorder() {
  const events = [];
  let endedResolve;
  const ended = new Promise(resolve => { endedResolve = resolve; });
  return {
    events,
    ended,
    callbacks: {
      response: value => events.push(["response", value]),
      chunk: (value, time) => events.push(["chunk", Array.from(value), time]),
      complete: value => events.push(["complete", value]),
      error: (code, detail) => events.push(["error", code, detail]),
      ended: () => endedResolve()
    }
  };
}

function response({ status = 200, url = "https://cdn.example/final", headers = [], body } = {}) {
  return {
    status,
    url,
    headers: new Headers(headers),
    body,
    ok: status >= 200 && status < 300
  };
}

function request(extra = {}) {
  return {
    url: "https://media.example/segment",
    method: "GET",
    headers: [],
    range: null,
    body: null,
    includeCredentials: false,
    connectTimeoutMs: 1000,
    maxRedirects: 20,
    priority: "normal",
    ...extra
  };
}

test("streams chunks before completion and reports final response metrics", async () => {
  let releaseSecond;
  let secondRequestedResolve;
  const secondRequested = new Promise(resolve => { secondRequestedResolve = resolve; });
  let reads = 0;
  const body = { getReader: () => ({
    read() {
      reads += 1;
      if (reads === 1) return Promise.resolve({ value: new Uint8Array([1, 2]), done: false });
      if (reads === 3) return Promise.resolve({ done: true });
      secondRequestedResolve();
      return new Promise(resolve => { releaseSecond = () => resolve({
        value: new Uint8Array([3]),
        done: false
      }); });
    },
    cancel() { return Promise.resolve(); }
  }) };
  const transport = createFetchTransport(async () => response({
    status: 206,
    url: "https://cdn.example/redirected",
    headers: [["content-range", "bytes 5-7/8"]],
    body
  }));
  const result = recorder();
  const handle = transport.start(request({ range: { offset: 5, length: 3 } }), result.callbacks);
  await secondRequested;
  assert.deepEqual(result.events.map(event => event[0]), ["response", "chunk"]);
  assert.equal(result.events[0][1].status, 206);
  assert.equal(result.events[0][1].finalUrl, "https://cdn.example/redirected");
  releaseSecond();
  await handle.completion;
  await result.ended;
  assert.deepEqual(result.events.map(event => event[0]), ["response", "chunk", "chunk", "complete"]);
  assert.deepEqual(result.events[2][1], [3]);
  assert.equal(result.events[3][1].bodyBytes, 3);
  assert.equal(result.events[3][1].coarseTiming, true);
});

test("applies range, credentials, priority and request hooks", async () => {
  let observed;
  const transport = createFetchTransport(async (url, init) => {
    observed = { url, init };
    return response({ body: null });
  }, {
    usePriorityHints: true,
    requestHook: async ({ url, init }) => ({
      url: `${url}?signed=1`,
      init: { headers: new Headers([...init.headers, ["x-token", "signed"]]) }
    })
  });
  const result = recorder();
  const handle = transport.start(request({
    range: { offset: 0 },
    includeCredentials: true,
    priority: "high"
  }), result.callbacks);
  await handle.completion;
  await result.ended;
  assert.equal(observed.url, "https://media.example/segment?signed=1");
  assert.equal(observed.init.headers.get("range"), "bytes=0-");
  assert.equal(observed.init.headers.get("x-token"), "signed");
  assert.equal(observed.init.credentials, "include");
  assert.equal(observed.init.priority, "high");
});

test("maps fetch TypeError to a CORS diagnostic and preserves HTTP status responses", async () => {
  const failed = createFetchTransport(async () => { throw new TypeError("Failed to fetch"); });
  const failure = recorder();
  await failed.start(request(), failure.callbacks).completion;
  await failure.ended;
  assert.equal(failure.events[0][0], "error");
  assert.equal(failure.events[0][1], "UNAVAILABLE");
  assert.match(failure.events[0][2], /probable CORS failure/);

  const http = createFetchTransport(async () => response({ status: 503, body: null }));
  const status = recorder();
  await http.start(request(), status.callbacks).completion;
  await status.ended;
  assert.equal(status.events[0][0], "response");
  assert.equal(status.events[0][1].status, 503);
  assert.equal(status.events[1][0], "complete");
});

test("rejects HTTPS downgrade redirects unless the request opts in", async () => {
  const transport = createFetchTransport(async () => response({
    url: "http://cdn.example/segment",
    body: null
  }));
  const refused = recorder();
  await transport.start(request(), refused.callbacks).completion;
  await refused.ended;
  assert.deepEqual(refused.events.map(event => event[0]), ["error"]);
  assert.equal(refused.events[0][1], "FAILED_PRECONDITION");

  const allowed = recorder();
  await transport.start(request({ allowHttpsDowngrade: true }), allowed.callbacks).completion;
  await allowed.ended;
  assert.equal(allowed.events[0][0], "response");
});

test("cancel during a body read suppresses completion and reports no error", async () => {
  let readStarted;
  const waiting = new Promise(resolve => { readStarted = resolve; });
  let readCount = 0;
  let finishRead;
  const body = { getReader: () => ({
    read() {
      readCount += 1;
      if (readCount === 1) return Promise.resolve({ value: new Uint8Array([1]), done: false });
      readStarted();
      return new Promise(resolve => { finishRead = resolve; });
    },
    cancel() {
      finishRead?.({ done: true });
      return Promise.resolve();
    }
  }) };
  const transport = createFetchTransport(async () => response({ body }));
  const result = recorder();
  const handle = transport.start(request(), result.callbacks);
  await waiting;
  handle.cancel();
  await handle.completion;
  await result.ended;
  assert.deepEqual(result.events.map(event => event[0]), ["response", "chunk"]);
});

test("pause holds the next read until resume", async () => {
  let reads = 0;
  let handle;
  const body = { getReader: () => ({
    read() {
      reads += 1;
      if (reads === 1) return Promise.resolve({ value: new Uint8Array([1]), done: false });
      if (reads === 2) return Promise.resolve({ value: new Uint8Array([2]), done: false });
      return Promise.resolve({ done: true });
    },
    cancel() { return Promise.resolve(); }
  }) };
  const transport = createFetchTransport(async () => response({ body }));
  const result = recorder();
  const callbacks = { ...result.callbacks };
  callbacks.chunk = (value, time) => {
    result.callbacks.chunk(value, time);
    if (reads === 1) assert.equal(handle.pause(), true);
  };
  handle = transport.start(request(), callbacks);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(reads, 1);
  const count = result.events.filter(event => event[0] === "chunk").length;
  assert.equal(result.events.filter(event => event[0] === "chunk").length, count);
  assert.equal(handle.resume(), true);
  await handle.completion;
  await result.ended;
  assert.equal(result.events.filter(event => event[0] === "complete").length, 1);
});

test("stop aborts active requests and rejects later starts", async () => {
  let fetchStarted;
  const started = new Promise(resolve => { fetchStarted = resolve; });
  const transport = createFetchTransport((_url, init) => new Promise((_resolve, reject) => {
    fetchStarted();
    init.signal.addEventListener("abort", () => reject(new DOMException("Aborted", "AbortError")));
  }));
  const result = recorder();
  const handle = transport.start(request(), result.callbacks);
  await started;
  assert.equal(transport.stop(), false);
  await handle.completion;
  await result.ended;
  assert.throws(() => transport.start(request(), result.callbacks), /stopped/);
  assert.deepEqual(result.events, []);
});
