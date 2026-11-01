# 0015 — Order engine work on sequenced task runners

- **Status:** Accepted
- **Date:** 2026-10-31
- **Deciders:** @LuisAhumadaMartens
- **Definition issue:** #1

## Context

A media pipeline mixes workloads with conflicting timing needs: strictly ordered control (state machine, ABR,
public API calls), network callbacks on threads the platform stack owns, CPU-bound demuxing of untrusted input,
decoders that block on hardware queues or run their own thread pools, a real-time audio callback, a vsync-driven
video presenter, and an embedder that lives on a UI or main thread.

The model must satisfy ThreadSanitizer in CI, deterministic tests on a fake clock, thread budgets on 1 GB-class
devices, and a web build that may have no threads at all: without cross-origin isolation (D-17, #17) there is no
`SharedArrayBuffer`, and the browser main thread may never block. Every later component inherits this choice.

## Decision drivers

- Component state without locks, and an order of work that is easy to reason about.
- One model that runs unchanged on a native thread pool and on a single-threaded browser event loop.
- Deterministic tests: no ad hoc threads, no reads of the system clock, no real sleeps.
- A bounded number of threads.
- A smooth player: public calls never block the embedder, and user actions are not queued behind background work.
- A model Chromium and Cobalt reviewers recognise.

## Considered options

1. **A — Sequences and task runners** (Chromium //base, Cobalt): closures posted to sequences, served by a
   bounded pool.
2. **B — Thread per component** (Starboard `PlayerWorker`, ExoPlayer, VLC).
3. **C — C++20 coroutines over an executor**.
4. **D — Thread pool with strands** (Asio).

## Decision

Chosen option: **A**, implemented as a small layer of our own. B cannot serve a threadless web build and makes
tests depend on real time. C needs a scheduler and cancellation library that C++20 does not provide. D gives the
same ordering guarantee as A, but Asio is built around sockets we will not use (networking is per platform,
D-08) and around `io_context::run()`, which blocks: on the web it would have to be pumped by hand, and fake time
needs custom clock traits. A's scheduler is small enough to own, and its cost per task (about a microsecond for
a few thousand tasks a second) is negligible next to demuxing and decoding.

### Layer

- Umbra defines `TaskRunner` and `SequencedTaskRunner` (`PostTask`, `PostDelayedTask`,
  `RunsTasksInCurrentSequence`), `SequenceChecker`, `WeakPtr`/`WeakPtrFactory` and a monotonic `TickClock`. The
  names follow Chromium; the code does not come from //base.
- The implementation uses the C++ standard library and Abseil (`absl::AnyInvocable`, `absl::Time`). Locks are a
  thin wrapper over `std::mutex` annotated for Clang's `-Wthread-safety` analysis; `absl::Mutex` is not used,
  because the pinned Abseil is not built with ThreadSanitizer and would report false races.
- Penumbra supplies the runners a platform must own: a bounded worker pool on native ports, the browser event loop
  or workers on the web (#143), and the Android looper or Apple main queue where an embedder's thread is needed.
- Tests use a `FakeTickClock` and a manual runner that runs due tasks by due time, then posting order.

### Sequences and priorities

- One **control** sequence: the playback state machine, ABR, buffering decisions and marshalled public API calls.
- One sequence per **stream**: demuxing and feeding that stream's decoder.
- One **network** sequence that receives callbacks from the platform stack.
- Two priorities: control work runs before background work such as prefetching, so a seek waits for at most the
  task already running on its sequence.
- Decoder libraries keep their own threads, within the thread budget below.

### Paths outside the scheduler

- The real-time audio callback never runs or posts tasks. Samples reach it through pre-allocated lock-free
  single-producer/single-consumer rings; Clang's RealtimeSanitizer checks this in CI where the toolchain supports
  it.
- The platform vsync callback pulls the due frame from a lock-free frame queue. Presentation policy belongs to
  D-02 (#2).
- Neither path is capped at a frame rate: video presentation follows the display's refresh rate, and content
  frame rates are independent of it.

### Blocking work

Nothing blocks on a sequence. Work that may block carries a `MayBlock` trait: native ports run it on dedicated
threads, and the single-threaded web build rejects it.

### Embedder contract

- **Delivery.** Events go into a per-player queue, and the engine calls a lightweight notify hook when the queue
  becomes non-empty ("notify, then pull", as in mpv). By default the SDKs install a hook that posts a drain task
  to a runner the embedder supplies, which defaults to the thread that created the player (the main thread on
  the web), as Media3 and AVFoundation do. Integrators, and a future C ABI, can drain the queue themselves.
- **Re-entrancy follows HTML media elements.** Any public method may be called from an event handler. Calls only
  post work, so they never recurse into the engine, and the events they cause are delivered after the current
  handler returns. Attributes that HTML updates synchronously (for example `paused` after `pause()` and
  `currentTime` after a seek) change immediately on the embedder side. Events are never dispatched while an
  internal lock is held. Destroying the player from a handler is allowed; its remaining events are dropped.
- **Coalescing.** High-frequency events are merged in the queue. `timeupdate` fires at most every 250 ms by
  default, as browsers do, and the interval is a setting.

### Time in tests

Task runners and the media clock (D-02) read one virtual time source in tests, so timers, retries, watchdogs and
playback position advance together. D-02 may layer deliberately skewed clocks on top to test drift.

### Thread budget

The worker pool size and a single budget for decoder threads are settings whose defaults derive from the
hardware concurrency, so a 1 GB-class TV device does not run a thread per core in every library. The numbers are
validated by M11 performance work rather than fixed now.

### Shutdown and cancellation

The embedder may destroy a player at any time. Teardown runs in a fixed order: stop accepting API calls, cancel
network requests, stop the port, drain the control sequence, release resources. Tasks and callbacks bound to
destroyed objects run as no-ops through weak pointers. Ports must not invoke callbacks after their stop completes;
D-04 (#4) records this in the port contract.

### Single-threaded web build

Every sequence runs on the browser event loop. The runner yields to the browser when its time budget is spent.
The default budget is half the display's frame interval, capped at 8 ms (about 4 ms at 120 Hz), and is a setting.
Long work such as parsing a large segment is written incrementally, so it can resume in the next task.

### Coroutines

Coroutines may be adopted later inside a component, resumed by these task runners. They are not a second
scheduler.

### Settings

The final names and schema belong to D-07 (#7). The provisional keys are `engine.threads.max_workers`,
`engine.threads.decoder_max`, `player.events.timeupdate_interval_ms` and `engine.threads.yield_budget_ms`.

## Consequences

**Positive**
- Components own their state without locks, and `SequenceChecker` asserts it in debug builds.
- The same engine code runs on a native pool and on one browser thread.
- Tests are deterministic and need no sleeps.
- The embedder thread never waits on engine work, and user actions take priority over background work.

**Negative / costs**
- We own a scheduler, weak pointers and a fake clock, and their bugs; TSan, the fake runner and benchmarks
  mitigate this.
- Asynchronous code produces "posted from" traces rather than straight call stacks.
- Parsers and other long work must be incremental to keep the web build responsive.

**Follow-ups**
- #48 implements the layer, the fake clock, the single-thread mode, `-Wthread-safety`, and a post-and-run
  latency benchmark baseline.
- #89 builds the playback state machine on the control sequence.
- D-02 (#2) defines the media clock on the shared virtual time and the presentation policy.
- D-04 (#4) adds "no callbacks after stop" and the callback sequence to the port contract.
- D-07 (#7) gives the provisional settings their final names.
- D-17 (#17) and #143 implement the web runners and the time budget.

## References

- [D-01 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/1)
- [Chromium: Threading and tasks](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks.md)
- [Chromium: Testing components that post tasks](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks_testing.md)
- [Cobalt: starboard/shared/starboard/player/job_queue.h](https://github.com/youtube/cobalt/blob/main/starboard/shared/starboard/player/job_queue.h)
- [Media3: A note on threading](https://developer.android.com/media/media3/exoplayer/hello-world#a-note-on-threading)
- [HTML: media element events (`timeupdate`)](https://html.spec.whatwg.org/multipage/media.html#event-media-timeupdate)
- [Emscripten: Pthreads support](https://emscripten.org/docs/porting/pthreads.html)
- [mpv: libmpv client API header](https://github.com/mpv-player/mpv/blob/master/include/mpv/client.h)
- [Clang: RealtimeSanitizer](https://clang.llvm.org/docs/RealtimeSanitizer.html)
- [Ross Bencina: Real-time audio programming 101](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing)
