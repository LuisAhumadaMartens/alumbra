const DEFAULTS = Object.freeze({
  credentials: "same-origin",
  usePriorityHints: true,
  requestHook: null
});

const PRIORITIES = Object.freeze({
  low: "low",
  normal: "auto",
  high: "high"
});

function createFetchTransport(fetchFunction = globalThis.fetch, options = {}) {
  if (typeof fetchFunction !== "function") {
    throw new TypeError("fetch is unavailable");
  }
  const configuration = { ...DEFAULTS, ...options };
  if (!["omit", "same-origin", "include"].includes(configuration.credentials)) {
    throw new TypeError("invalid fetch credentials mode");
  }
  const transfers = new Set();
  let stopped = false;

  function start(request, callbacks) {
    if (stopped) {
      throw new Error("transport is stopped");
    }
    if (!request || typeof request.url !== "string" || request.url.length === 0 || !callbacks) {
      throw new TypeError("invalid HTTP request");
    }
    if (request.method !== "GET" && request.method !== "HEAD" && request.method !== "POST") {
      throw new TypeError("invalid HTTP method");
    }
    if (request.method !== "POST" && request.body != null && request.body.byteLength !== 0) {
      throw new TypeError("request body requires POST");
    }
    if (request.range && request.range.length === 0) {
      throw new TypeError("invalid byte range");
    }
    if (!Number.isFinite(request.connectTimeoutMs) || request.connectTimeoutMs <= 0) {
      throw new TypeError("invalid connect timeout");
    }
    if (!Number.isInteger(request.maxRedirects) || request.maxRedirects < 0 || request.maxRedirects > 20) {
      throw new TypeError("invalid redirect limit");
    }

    const controller = new AbortController();
    const transfer = {
      controller,
      cancelled: false,
      terminal: false,
      paused: false,
      reader: null,
      resume: null,
      timeout: null,
      requestStart: performance.now(),
      responseStart: null,
      bodyBytes: 0
    };
    transfers.add(transfer);
    const headers = new Headers(request.headers || []);
    if (request.range) {
      const end = request.range.length == null
        ? ""
        : String(request.range.offset + request.range.length - 1);
      headers.set("Range", `bytes=${request.range.offset}-${end}`);
    }
    const init = {
      method: request.method,
      headers,
      body: request.method === "POST" ? request.body : undefined,
      credentials: request.includeCredentials ? "include" : configuration.credentials,
      redirect: "follow",
      signal: controller.signal
    };
    if (configuration.usePriorityHints && request.priority in PRIORITIES) {
      init.priority = PRIORITIES[request.priority];
    }
    const pauseUntilResumed = () => transfer.paused
      ? new Promise(resolve => { transfer.resume = resolve; })
      : Promise.resolve();

    const run = async () => {
      let response;
      let requestHookFinished = configuration.requestHook === null;
      try {
        let url = request.url;
        if (configuration.requestHook) {
          const hooked = await configuration.requestHook({ url, init });
          if (hooked) {
            if (typeof hooked.url === "string") {
              url = hooked.url;
            }
            if (hooked.init) {
              Object.assign(init, hooked.init);
            }
          }
          requestHookFinished = true;
        }
        init.signal = controller.signal;
        init.redirect = "follow";
        transfer.timeout = setTimeout(() => controller.abort("connect timeout"), request.connectTimeoutMs);
        response = await fetchFunction(url, init);
      } catch (error) {
        if (transfer.cancelled || controller.signal.aborted) {
          if (!transfer.cancelled && controller.signal.reason === "connect timeout") {
            callbacks.error("DEADLINE_EXCEEDED", "fetch request timed out before response headers");
          }
          return;
        }
        const diagnostic = !requestHookFinished
          ? `request hook failed: ${String(error)}`
          : error instanceof TypeError
          ? `probable CORS failure or network error: ${error.message}`
          : `network error: ${String(error)}`;
        callbacks.error("UNAVAILABLE", diagnostic);
        return;
      } finally {
        clearTimeout(transfer.timeout);
        transfer.timeout = null;
      }
      if (transfer.cancelled) {
        return;
      }
      if (new URL(request.url).protocol === "https:" &&
          new URL(response.url).protocol === "http:" &&
          !request.allowHttpsDowngrade) {
        callbacks.error("FAILED_PRECONDITION", "HTTPS to HTTP redirect refused");
        return;
      }
      transfer.responseStart = performance.now();
      transfer.status = response.status;
      callbacks.response({
        status: response.status,
        headers: Array.from(response.headers.entries()),
        finalUrl: response.url,
        protocol: protocolFor(response.url)
      });
      if (!response.body || request.method === "HEAD") {
        finish();
        return;
      }
      transfer.reader = response.body.getReader();
      while (!transfer.cancelled) {
        await pauseUntilResumed();
        if (transfer.cancelled) {
          break;
        }
        let result;
        try {
          result = await transfer.reader.read();
        } catch (error) {
          if (!transfer.cancelled) {
            callbacks.error("UNAVAILABLE", `response body read failed: ${String(error)}`);
          }
          return;
        }
        if (result.done) {
          finish();
          return;
        }
        if (transfer.cancelled) {
          break;
        }
        const chunk = result.value;
        transfer.bodyBytes += chunk.byteLength;
        callbacks.chunk(chunk, performance.now());
      }
    };

    const finish = () => {
      if (transfer.terminal || transfer.cancelled) {
        return;
      }
      transfer.terminal = true;
      const completion = performance.now();
      const timing = timingFor(request.url);
      callbacks.complete({
        status: transfer.status ?? 0,
        requestStart: transfer.requestStart,
        responseStart: transfer.responseStart ?? completion,
        completion,
        bodyBytes: transfer.bodyBytes,
        protocol: timing?.nextHopProtocol ? protocolFromName(timing.nextHopProtocol) : "unknown",
        coarseTiming: true
      });
    };

    const task = run().finally(() => {
      transfer.terminal = true;
      transfers.delete(transfer);
      callbacks.ended?.();
    });
    return {
      cancel() {
        if (transfer.cancelled || transfer.terminal) {
          return;
        }
        transfer.cancelled = true;
        controller.abort();
        transfer.resume?.();
        transfer.reader?.cancel().catch(() => {});
      },
      pause() {
        if (transfer.cancelled || transfer.terminal) {
          return false;
        }
        transfer.paused = true;
        return true;
      },
      resume() {
        if (transfer.cancelled || transfer.terminal) {
          return false;
        }
        transfer.paused = false;
        transfer.resume?.();
        transfer.resume = null;
        return true;
      },
      completion: task
    };
  }

  function finish() {
    stopped = true;
    for (const transfer of transfers) {
      transfer.cancelled = true;
      transfer.controller.abort();
      transfer.resume?.();
      transfer.reader?.cancel().catch(() => {});
    }
    return transfers.size === 0;
  }

  return { start, stop: finish };
}

const moduleStates = new WeakMap();

function moduleState(module) {
  let state = moduleStates.get(module);
  if (!state) {
    state = { transports: new Map(), handles: new Map() };
    moduleStates.set(module, state);
  }
  return state;
}

function moduleKey(core, identifier) {
  return `${core}:${identifier}`;
}

function allocateString(module, value) {
  const size = module.lengthBytesUTF8(value) + 1;
  const pointer = module._malloc(size);
  module.stringToUTF8(value, pointer, size);
  return pointer;
}

function create(module, core, credentials, usePriorityHints) {
  const modes = ["omit", "same-origin", "include"];
  moduleState(module).transports.set(core, createFetchTransport(globalThis.fetch, {
    credentials: modes[credentials],
    usePriorityHints: usePriorityHints !== 0,
    requestHook: request => module.AlumbraRequestHook?.(request)
  }));
}

function start(module, core, identifier, requestPointer, bodyPointer, bodySize) {
  const state = moduleState(module);
  const transport = state.transports.get(core);
  const request = JSON.parse(module.UTF8ToString(requestPointer));
  request.body = bodySize === 0 ? null : module.HEAPU8.slice(bodyPointer, bodyPointer + bodySize);
  const key = moduleKey(core, identifier);
  const callbacks = {
    response: head => {
      module._alumbra_web_http_response_begin(
        core, identifier, head.status,
        ["unknown", "http1_0", "http1_1", "http2", "http3"].indexOf(head.protocol),
        allocateString(module, head.finalUrl));
      for (const [name, value] of head.headers) {
        module._alumbra_web_http_response_header(
          core, identifier, allocateString(module, name), allocateString(module, value));
      }
      module._alumbra_web_http_response_end(core, identifier);
    },
    chunk: (bytes, arrival) => {
      const pointer = module._malloc(bytes.byteLength);
      module.HEAPU8.set(bytes, pointer);
      module._alumbra_web_http_chunk(core, identifier, pointer, bytes.byteLength, arrival);
    },
    complete: metrics => module._alumbra_web_http_complete(
      core, identifier, metrics.status, metrics.requestStart, metrics.responseStart,
      metrics.completion, metrics.bodyBytes,
      ["unknown", "http1_0", "http1_1", "http2", "http3"].indexOf(metrics.protocol),
      metrics.coarseTiming ? 1 : 0),
    error: (code, detail) => module._alumbra_web_http_error(
      core, identifier,
      code === "DEADLINE_EXCEEDED" ? 1 : code === "FAILED_PRECONDITION" ? 2 : 0,
      allocateString(module, detail)),
    ended: () => {
      state.handles.delete(key);
      module._alumbra_web_http_ended(core, identifier);
    }
  };
  state.handles.set(key, transport.start(request, callbacks));
}

function control(module, core, identifier, action) {
  const handle = moduleState(module).handles.get(moduleKey(core, identifier));
  if (!handle) return;
  if (action === 0) handle.cancel();
  if (action === 1) handle.pause();
  if (action === 2) handle.resume();
}

function destroy(module, core) {
  moduleState(module).transports.delete(core);
}

globalThis.AlumbraFetchTransport = Object.freeze({
  create,
  createFetchTransport,
  control,
  destroy,
  start
});

function protocolFor(url) {
  const timing = timingFor(url);
  return timing?.nextHopProtocol ? protocolFromName(timing.nextHopProtocol) : "unknown";
}

function timingFor(url) {
  if (typeof performance === "undefined" || typeof performance.getEntriesByName !== "function") {
    return undefined;
  }
  return performance.getEntriesByName(url, "resource").at(-1);
}

function protocolFromName(value) {
  if (value === "http/1.0") return "http1_0";
  if (value === "http/1.1") return "http1_1";
  if (value === "h2") return "http2";
  if (value === "h3") return "http3";
  return "unknown";
}
