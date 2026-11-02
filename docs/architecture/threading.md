# Threading primitives

[ADR-0015](../adr/0015-threading-and-task-runners.md) orders engine work on sequenced task runners. This page
documents the `umbra::base` layer that implements it (#48): what each type does, the lifetime and threading rules
callers must follow, and the test support. Code comments are not used in Alumbra, so these rules live here.

## Types

| Header | Type | Purpose |
|-------------------------|------|---------|
| `task_runner.h` | `TaskRunner`, `SequencedTaskRunner` | `PostTask`, `PostDelayedTask`, `RunsTasksInCurrentSequence`. Every post records a `std::source_location`. |
| `task_runner.h` | `TaskPriority` | `kControl` sequences run before `kBackground` sequences. |
| `task_runner.h` | `TaskExecutor` | Creates sequences: `CreateSequencedTaskRunner(TaskPriority)`. Components depend on this interface, not on a concrete executor. |
| `thread_pool.h` | `ThreadPool` | Native executor: a bounded set of workers multiplexing any number of sequences. |
| `penumbra/interface/thread.h` | `penumbra::ThreadFactory`, `penumbra::ThreadHandle` | Hook through which a port creates the pool's threads (naming, priorities, JVM attachment). It is standard C++ only, so a port implements it without Umbra or Abseil headers; `GetDefaultThreadFactory()` in `thread_pool.h` starts plain `std::thread`s. |
| `single_thread_task_executor.h` | `SingleThreadTaskExecutor` | Every sequence on one thread, pumped by its owner with a time budget: the model of the single-threaded web build. |
| `sequence_checker.h` | `SequenceChecker` | Asserts that an object is used on one sequence. |
| `sequence_token.h` | `SequenceToken`, `ScopedTaskContext`, `GetCurrentTaskPostedFrom` | Identity of the running sequence; executors set it while a task runs. |
| `weak_ptr.h` | `WeakPtr`, `WeakPtrFactory` | Tasks bound to a destroyed owner become no-ops. |
| `tick_clock.h`, `time_ticks.h` | `TickClock`, `TimeTicks`, `GetDefaultTickClock` | Monotonic time. Components read time through an injected `TickClock`, never the system clock. |
| `lock.h` | `Lock`, `AutoLock`, `ConditionVariable` | Annotated mutex for `-Wthread-safety` (see [Locks](#locks)). |

The `umbra/base/` headers above are internal engine headers. They live in `umbra/base/internal_include/`, are
visible to every target inside the build and are **not installed**, because they use Abseil
(`absl::AnyInvocable`, thread-safety annotations). Installed headers use only the C++20 standard library; see
[Public headers](overview.md#public-headers).

Test support lives in `umbra/base/test_support/` (target `umbra::base_test_support`, not installed):
`FakeTickClock`, `TestTaskEnvironment` (fake clock plus a single-thread executor with `RunUntilIdle`,
`FastForwardBy` and `NextPendingTaskDelay`) and `WaitableEvent`.

## Rules

**Posting.** `PostTask` and `PostDelayedTask` may be called from any thread. They return `false` when the executor
has shut down or been destroyed; the task is then destroyed without running. A negative delay counts as zero.
Tasks are `absl::AnyInvocable<void() &&>`, so they may own move-only state and run at most once.

**Ordering.** Tasks of one sequence run one at a time and never concurrently. Immediate tasks run in posting
order. A delayed task joins its sequence's queue once it is due, behind tasks already queued; delayed tasks with
the same due time keep their posting order. `TestTaskEnvironment` uses the single-thread executor's
`Ordering::kChronological` mode: all sequences run strictly by due time and then posting order, irrespective of
priority. Production single-thread executors default to `Ordering::kPriority`, selecting due control work
before due background work and ordering each priority by due time and posting order.
Different sequences run in parallel on a `ThreadPool`. When
several sequences are ready, `kControl` ones run first; there is no starvation protection for `kBackground`
work, so control sequences must not post unbounded work to themselves.

**Task lifetime.** A task's captured state is destroyed right after it runs, inside its sequence context, so
destructors may use `SequenceChecker` and `WeakPtr`. Tasks dropped by a shutdown are destroyed outside any
sequence context and on whichever thread drops them.

**Sequence affinity.** `SequenceChecker` binds to the sequence that constructs it (or, after
`DetachFromSequence`, to the next caller). `CalledOnValidSequence` always performs the check; callers wrap it in
`assert`, so the check costs nothing in release builds. Outside any task, each thread counts as its own sequence.

**Weak pointers.** Dereference a `WeakPtr`, and invalidate or destroy its `WeakPtrFactory`, on the same sequence;
debug builds assert this. Declare the factory as the owner's last member so it is destroyed first. A `WeakPtr`
may be copied, moved and converted across sequences, even after invalidation. Base-class pointer adjustment
is deferred until a validated dereference, including virtual-base conversions.

**Blocking.** Tasks must not block. `WaitableEvent` exists only for tests and benchmarks.

**Thread pool.** `ThreadPoolOptions::max_workers` of `0` derives the worker count from the hardware:
`clamp(hardware_concurrency - 1, 2, 4)`. This is the provisional setting `engine.threads.max_workers`; D-07 (#7)
fixes its name and M11 validates the numbers. `Shutdown` (also run by the destructor) stops accepting tasks,
drops queued and delayed tasks, waits for running tasks to finish and joins the workers. It must be called from
outside the pool. Queued tasks of a sequence whose task is running when shutdown starts are dropped as soon as
that task returns. A worker claims its next task under the same lock that starts shutdown; a claimed task is
running for this contract, even if its callback has not entered yet. Due delayed tasks are promoted under that
lock before a later immediate post joins a sequence. An idle worker assumes the timer wait when another worker
takes ready work. The `ThreadFactory` passed in the options must outlive the pool, return a non-null handle for
every successfully started worker, and report failures by throwing. Construction shuts down and joins already
started workers before propagating a startup failure. `ThreadPoolOptions::tick_clock` optionally injects the
time source for tests; that clock must remain alive until shutdown returns.

**Single-thread executor.** The owner pumps it from one thread: `RunUntilIdle` runs every due task, including
tasks posted meanwhile; `RunFor(budget)` stops once the budget is spent and returns whether due work remains;
`NextTaskTime` tells the host when to pump next. Posts may come from any thread. The optional wake-up callback
runs on the posting thread whenever a post becomes the earliest pending task, so a host loop can reschedule
itself. Wake-ups are serialised with posting and shutdown, outside the task-queue lock. The hook must be
lightweight and must not wait for another thread to post or shut down this executor. It may post or call
`Shutdown` reentrantly. Shutdown waits for any wake-up on another thread to finish and prevents subsequent
wake-ups or borrowed-clock reads from posting calls. A reentrant shutdown finishes inside the current hook;
the hook must not use torn-down host state afterwards. Do not pump from inside a task.

**Fake time.** `FastForwardBy` never rewinds time: a negative delta leaves the clock unchanged, and a task that
advances beyond the requested target keeps that later time. The fake-clock sleep lint follows local fixture
header dependencies and matches multiline calls, including POSIX sleep APIs.

## Locks

Clang's `-Wthread-safety` is enabled for every Alumbra target, with Abseil's annotation macros
(`ABSL_GUARDED_BY`, `ABSL_EXCLUSIVE_LOCKS_REQUIRED`). The annotated lock is `umbra::base::Lock`, a thin wrapper
over `std::mutex`, rather than `absl::Mutex`: vcpkg builds Abseil without ThreadSanitizer instrumentation, so TSan
cannot see `absl::Mutex` synchronisation and reports false races on data it protects, while `std::mutex` goes
through the `pthread` calls TSan intercepts. If sanitizer builds later instrument their dependencies,
`absl::Mutex` can replace the wrapper.

## Benchmark baseline

`task_runner_benchmark` (Google Benchmark) measures post-and-run cost. Recorded on 2026-10-31 on a 14-core
Apple Silicon Mac (arm64), Xcode 27.2 (Apple Clang 21, libc++ 22.1.6), `release` preset, median of three
repetitions:

| Benchmark | What it measures | Wall time | Throughput |
|-----------|------------------|-----------|------------|
| `BM_ThreadPoolPostAndRunBatch` | Post 1000 tasks to one sequence of a two-worker pool and wait until all ran | 197 µs per batch (≈197 ns per task) | 5.1 M tasks/s |
| `BM_ThreadPoolCrossSequenceRoundTrip` | Post to sequence A, which posts to sequence B, which wakes the waiting thread | 7.8 µs per round trip | 128 k round trips/s |
| `BM_SingleThreadPostAndRunBatch` | Post 1000 tasks and `RunUntilIdle` on one thread | 93 µs per batch (≈93 ns per task) | 10.7 M tasks/s |

The round trip is dominated by waking sleeping threads, not by the queues.

Run it with `build/release/test/umbra/base/task_runner_benchmark`. CTest runs a short smoke pass under the
`benchmark` label.
