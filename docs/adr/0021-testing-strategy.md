# 0021 — Use deterministic in-process testing with mandatory real-port conformance

- **Status:** Accepted on merge of this ADR pull request
- **Date:** 2026-11-01
- **Deciders:** Project coordinator under @LuisAhumadaMartens's delegated autonomous authority;
  acceptance through coordinator review and merge
- **Definition issue:** #31

## Context

A media player can pass unit tests and still drift, stall, mishandle a seek, or fail on one decoder or
device. Testing must both localise engine defects and demonstrate the behaviour of each claimed platform
configuration. Fake ports and simulators are useful test tools, not evidence of real hardware support.

[ADR-0009](0009-toolchain.md) selects GoogleTest, Google Benchmark, sanitizers and libFuzzer.
[ADR-0015](0015-threading-and-task-runners.md) supplies virtual time and deterministic task ordering.
[ADR-0016](0016-penumbra-port-contract.md) requires every port to honour a shared contract.
[ADR-0012](0012-repository-topology-and-packaging.md) requires path-filtered CI and fetched, not committed,
large media. [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md#cost-budget) fixes observability
overhead requirements. This record complements those accepted decisions without changing their bodies.

The [reviewed brief](https://github.com/LuisAhumadaMartens/alumbra/issues/31#issuecomment-6554694989)
remained a proposal for owner review. When the focused owner-choice request returned that the owner was
unavailable and directed autonomous judgment, the coordinator selected the policy in the
[decision note](https://github.com/LuisAhumadaMartens/alumbra/issues/31#issuecomment-6555064887).
This is a coordinator-selected decision under delegated authority, **not a claim that Luis personally
reviewed or approved this design**. The coordinator's review and merge accepts this ADR; #31 remains open
until that merge, following [ADR-0001](0001-record-architecture-decisions.md).

## Decision drivers

- Fast, reproducible merge checks with useful failure localisation and no real sleeps in virtual-time tests.
- Contract evidence from the actual port and configuration being claimed, including device-specific paths.
- Relevant Linux and macOS coverage despite runner scarcity.
- Legally usable, offline-capable inputs with traceable provenance and integrity.
- No missing, skipped, flaky or simulated result misrepresented as verified release coverage.
- Measured performance claims without invented thresholds, hardware stability or service availability.

## Considered options

1. **A -- Deterministic in-process pyramid:** most checks use fakes and virtual time; smaller platform and
   device suites exercise real integrations.
2. **B -- End-to-end first:** most confidence comes from real streams, browsers and devices.
3. **C -- Conformance-first:** prioritise port and stream conformance, adding unit tests to localise failures.

## Decision

Chosen option: **A, with C's conformance kit mandatory for every real port.** In-process tests provide
repeatable engine coverage; real-port, platform and output tests provide the evidence fakes cannot.

### Current checks and staged gates

The implemented CI jobs at the time of recording are described in [process](../process.md#continuous-integration):
change classification, workflow lint/helper checks, error-catalogue and settings checks, and the
path-filtered Linux/macOS debug/release build/test matrix. This documentation change adds no jobs,
providers, assets or devices and certifies none of them.

The layers below are **implementation commitments**, not a report of already-passing jobs. Their tasks
must establish the checks and make the applicable merge gates blocking as they land. An unavailable
release check remains an explicit evidence gap; staging infrastructure does not waive a release gate
for a configuration claimed as verified.

| Layer | Merge commitment and execution | Release evidence and ownership |
|-------|--------------------------------|--------------------------------|
| Unit and deterministic integration | GoogleTest for parsers, ABR, clocks, settings generation and the headless UI state machine's golden ViewModels; fake network/time/ports on relevant Linux/macOS legs | Reuse applicable cases; #65 defines layout, labels and the daily guide |
| Sanitizers | Linux Clang ASan+UBSan and separate TSan; macOS ASan+UBSan, with instrumented dependencies (#44) | Applicable suites pass on the instrumented configuration; unsupported sanitizers do not imply coverage |
| Parser fuzzing | Bounded libFuzzer smoke on parser-relevant PRs, plus portable corpus/regression replay on all compiler legs (#45) | Longer scheduled corpus-growth and provider work belongs to #267; findings remain traceable |
| Stream conformance | Small fetched/generated offline fixtures for affected behaviour (#64/#65) | Full applicable local corpus with expected outcomes for each claimed configuration (#268) |
| Golden media and text | Deterministic output checks for affected decoders/renderers; text cases on Linux/macOS when implemented (#141) | Demonstrated exact hashes or reviewed, calibrated comparisons for the specific output path |
| Network and ABR | Virtual-time synthetic impairment scenarios using production ABR logic (#122) | Applicable real-stack impairment checks, with targeted Linux netem where supported |
| Port conformance | Target-executable kit against fake and actual ports on relevant platform legs (#71) | Actual port/configuration kit results for every tier-1 configuration; fake results certify no hardware |
| Platform UI and playback | Path-filtered browser/native integration checks as each platform lands; retain relevant macOS gates | Applicable browser/native suites and D-22 reference-device results, including tier-2 references |
| A/V sync | Deterministic clock/scheduling tests, distinct from output measurement | Captured presented video and audible output, using D-02's signed definition and tolerance |
| Performance | Stable metrics on shared runners; paired timing/CPU evidence only on a controlled host (#263) | Reviewed workload/baseline evidence, including ADR-0020's unchanged overhead requirements |

Keep the existing relevant Linux and macOS merge coverage and the
[ADR-0012 path-filtering policy](0012-repository-topology-and-packaging.md#decision). Scarce macOS
capacity is not a reason to remove a relevant gate. Documentation-only changes may retain the existing
C++ matrix skip; changes without a safe comparable base run the conservative full matrix.

### Determinism, inputs and conformance data

Use ADR-0015's injectable clock and manual runner for timers, retry, timeout, live-window and scheduling
tests. Advance virtual time rather than sleeping. Explicit seeds and bounded scenarios make failures
reproducible; deterministic control of a fake does not prove a physical decoder, clock or sink.

Start the corpus with **project-generated patterns**: frame-numbered video, tones, flashes/beeps and
synthetic network impairments. Commit generation/configuration scripts, not large media, per ADR-0012.
Generated inputs and fonts must still have recorded provenance and applicable redistribution rights.

Mirrored third-party inputs require independently verified, **asset-specific redistribution permission**,
attribution and SHA-256 integrity metadata before use. Availability at an Apple, CTA WAVE, DASH-IF or
other public URL is not permission to redistribute; a tool's licence does not license its sample assets.
Record the licence evidence and any restrictions, rather than inferring rights from a tool or website.
#64's fetcher must fail visibly on missing rights metadata, integrity mismatch or unavailable required
offline inputs; it must not silently substitute another asset.

Use a **versioned YAML conformance manifest** with stable case IDs. Record input/generator identity,
source and provenance, rights/attribution evidence, SHA-256 and size for downloadable assets,
protocol/container/codec/features and applicable configurations. Expected outcomes include track lists,
integer or rational durations with explicit units, ordered events or named errors, and optional
frame-reference IDs. Separate the manifest's schema version from result identities.

#64/#65 own the exact field layout, schema validation, initial corpus, reference storage/fetching and
test labels. This ADR selects the direction, not an implemented schema. #268 owns the result format and
dashboard; results identify the case, source revision, manifest/input versions, port/interface version,
decoder, build/toolchain, OS/browser/device and output mode, with per-case status and evidence links.

For live tests, record a reproducible **local generator/server configuration, seed and finite observation
window**, including window units and expected events. Pin fetched finite inputs and configuration where
applicable, but do not invent a fixed duration or immutable content hash for a moving public live URL.
Public live and optional third-party DRM endpoints are separately labelled, non-blocking probes. An
outage is reported as skipped/unavailable with a reason, not green evidence for a required release case.
#246 must keep deterministic local encrypted cases separate from optional external licence services.

### Golden output and actual A/V sync

Exact frame hashes are valid only for a decoder/build/configuration **demonstrated to be deterministic**,
not every software decoder by assumption. Pin the relevant input, decoder version, fonts, output format,
viewport/scaling and colour configuration, and retain reference provenance. Hardware-decoded or scaled
output uses calibrated comparisons with references for that path.

No universal SSIM, PSNR, VMAF or other image threshold is selected here. #64/#65 and #141 must obtain
reviewed per-case/per-configuration metrics and tolerances, demonstrate that intended regressions fail
(including #141's layout fixtures), and document reference updates. Until then the corresponding golden
release evidence is unresolved, not implicitly satisfied.

Clock ordering, drift injection and timestamp assertions remain deterministic in-process tests.
**Actual A/V sync** is measured separately with flash/beep content and validated capture of presented
video and audible audio: suitable loopback/frame capture where it represents the real output, or
physical light-sensor/microphone capture where hardware, overlays or secure output hide that path.
Decoder timestamps or a fake audio sink alone cannot prove presented-output sync.

[D-02](https://github.com/LuisAhumadaMartens/alumbra/issues/2) owns the signed offset definition,
tolerance, calibration/measurement method and route/startup/steady-state conditions. No numeric sync
threshold from its proposal is accepted by this ADR. A claimed output path requires that definition and
matching measured evidence before its sync coverage is verified.

### Network simulation and real-port/platform coverage

Use the production estimator/rules with virtual-time link, buffer and playback models. Synthetic
steps, outages and changing throughput are the first reproducible traces; #122 owns deterministic
reports and the ABR regression criteria under D-11.

Add targeted **Linux netem** real-network-stack impairment checks when the runner supports the required
privileges and a scenario warrants them. Report unavailable capabilities explicitly. Mahimahi and
external trace datasets are deferred until a concrete scenario justifies them and redistribution rights
are verified. Supporting a trace format does not require adopting its emulator or downloading a dataset.
Neither a simulator nor Linux netem establishes coverage of another platform's network stack.

The **M1 port kit** is a GoogleTest suite parameterised by a port factory, packaged so each port links a
target-executable binary (#71). It exercises ADR-0016 lifecycle/creation limits/destruction with pending
work, seek/flush ticket invalidation, write flow control, end of stream, media-time reporting, callback
sequence/no-callbacks-after-stop rules and supported output/capability/error paths. Broken fake fixtures
must fail named tests; the fake validates the kit and engine logic, never certifies a real port.
Headless real-port runs prove only the exercised headless configuration, not unexercised hardware output.

Browser suites use Playwright for Chromium-based coverage through the **`chrome` channel** when the
required codecs need official Chrome, plus Firefox and WebKit. Playwright's patched Firefox/WebKit and
emulated devices are recorded as such; WebKit is not Safari. Actual Safari on macOS is driven through
**`safaridriver`** and must provide its own result. D-22 determines additional real browser/device cells,
rather than inferring them from a three-engine suite.

Android instrumentation on emulators and XCTest/XCUITest on Apple simulators provide platform/UI
coverage where applicable, not proof of physical hardware decoders or commercial DRM. Apple simulator
results do not establish FairPlay coverage. #246 owns the encrypted conformance cases and actual
key-system/output-path evidence.

[D-22](https://github.com/LuisAhumadaMartens/alumbra/issues/22) owns the support tiers, exact
configurations and confirmed device inventory. Release candidates require the applicable offline corpus,
goldens, port/platform suites and actual A/V capture for each claimed configuration, including real-port
results for tier 1 and the designated tier-2 reference-device runs. Results link to the compatibility
matrix through #268. No owned device, TV/DRM capability or available cloud slot is assumed.
Cloud services are optional supplements after device/catalogue and capability verification; this ADR
authorises **no purchases or paid enrollment**.

### Fuzzing, sanitizers and failure handling

Keep ADR-0009's tools. #45 establishes the reusable libFuzzer harness, bounded parser-PR smoke and
standalone corpus replay on all compiler legs. Each parser of untrusted input needs a registered target:
the byte/bit readers first, then HLS, DASH, MP4 boxes, MPEG-TS, WebVTT/TTML, VAST/VMAP, ID3/`emsg` and the
wider inventory in #267 as those parsers land.

#45 must **measure and fix** run/seed/RSS/timeout budgets (`-runs`, `-seed`, `-rss_limit_mb`, `-timeout`)
on the selected runner. No guessed constants are introduced here. Crashes, leaks, OOM and timeouts fail
the smoke gate and retain reproducible evidence; runner constraints do not turn findings into success.

#267 owns longer scheduled corpus growth, pruning/coverage reports and later provider integration,
including the proposed ClusterFuzzLite work. This ADR introduces no provider dependency and promises
no already-running hosted service. Apply to OSS-Fuzz only after demonstrating its actual eligibility
(significant user base and/or critical infrastructure relevance) and obtaining acceptance, not at an
assumed milestone.

Minimise crashes, retain reproducers and add regression replay for fixes. Ordinary non-security defects
can use public issues; suspected vulnerabilities follow [SECURITY.md](../../SECURITY.md)'s private
advisory process before disclosure. Do not automatically publish security-sensitive crashes or
reproducers in public issues, logs or artifacts. #267's automatic filing must respect that boundary.

Reflect #44: **Linux ASan+UBSan and separate TSan, macOS ASan+UBSan**, with dependencies instrumented
for the selected sanitizer, not just the final test executable. Record toolchain/runtime limits and
reviewed, issue-linked suppressions. TSan covers concurrency and the applicable instrumented suite.
**MSan is conditional future work** requiring a supported toolchain/platform and fully instrumented
code/dependencies; there is no blanket Linux/macOS or third-party-library availability claim.

No known flake is silently converted to a pass. Preserve the **first failure** and its diagnostics;
diagnostic reruns supplement that evidence and do not replace a failed merge/release gate. There is no
positive tolerated flake rate for blocking checks. Quarantine requires reviewed owner, issue, expiry,
reason and coverage impact, with visible quarantined status in CI (#65). Quarantined/skipped/missing
cases cannot count as verified release coverage. If a required case is quarantined, its evidence gap
still blocks the relevant verified release claim.

### Performance evidence

Keep Google Benchmark microbenchmarks and end-to-end TTFF, CPU/frame, memory and other QoE metrics.
#263 defines reviewed budgets from measured baselines; it owns workload identity, same-host paired
comparisons, warm-up/repetition, uncertainty and baseline provenance before a compliance claim.

Preserve ADR-0020 exactly: **runtime-disabled observability overhead must be within benchmark noise**
against the equivalent workload without observability call sites; **enabled tracing adds at most 3%
CPU** on the reference desktop workload against trace capture disabled, using the same build, stream,
device and playback conditions. Record both baseline types explicitly; a compiled-out build alone
does not demonstrate runtime-disabled overhead.

Shared runners gate only demonstrated stable metrics such as package/WASM size, allocation counts or
instruction counts. Wall-clock, frame timing and CPU overhead need controlled same-host paired runs
and reported uncertainty. A runner label is not proof of stable physical hardware. No benchmark job,
baseline or overhead compliance is certified by this ADR.

## Consequences

**Positive**
- Deterministic engine checks localise failures while real-port/output suites keep compatibility claims honest.
- Licensing, integrity, configuration identity and first-failure evidence are part of the test contract.
- Existing Linux/macOS coverage and accepted tool, port, threading and overhead decisions are preserved.

**Negative / costs**
- Real-port, Safari and device-output evidence costs more than fake/headless testing and needs explicit access.
- Offline assets, calibrated goldens, instrumented dependencies and controlled benchmarks require maintenance.
- A missing device or unresolved tolerance can prevent a verified release claim even when fast CI is green.

**Follow-ups**
- #44 implements the sanitizer gates and dependency instrumentation; #45 measures the fuzz budgets and
  implements smoke/replay. #267 owns scheduled growth/provider work and private-safe crash triage.
- #64/#65 settle the versioned manifest layout, real asset rights/provenance, initial corpus/reference
  storage, labels and quarantine workflow. Rights remain unverified until asset-specific review.
- #64/#65 and #141 settle calibrated image metrics/thresholds and reference-update review. No universal
  threshold is selected; corresponding evidence remains gated on that work.
- D-02 settles signed A/V error, capture calibration and tolerances; D-22 settles support tiers, device
  inventory and release configuration scope. Neither owner decision is invented here.
- #71 implements the target-executable real-port kit; #122 implements virtual-time ABR scenarios and
  justified real-stack impairments; #246 implements DRM cases without treating endpoint skips as proof.
- #263 establishes controlled performance baselines and uncertainty; #268 establishes result/dashboard
  identity and compatibility generation. These tasks are not marked implemented or verified by this ADR.

## Pros and cons of the options

### A. Deterministic in-process pyramid with mandatory real-port conformance
- Good: fast, cheap, repeatable engine checks; actual-port evidence remains mandatory.
- Bad: fakes can miss device-specific defects, so the real-port/output/device layers cannot be optional.

### B. End-to-end first
- Good: exercises user-visible platform integrations early.
- Bad: slow and harder to reproduce/localise; public moving streams and scarce devices weaken merge checks.

### C. Conformance-first alone
- Good: prioritises common contract and stream behaviour.
- Bad: weak isolation of ABR/parser/state-machine faults; retain its kit without replacing in-process tests.

## References

- [D-31 discussion](https://github.com/LuisAhumadaMartens/alumbra/issues/31),
  [reviewed brief](https://github.com/LuisAhumadaMartens/alumbra/issues/31#issuecomment-6554694989) and
  [coordinator-selected policy](https://github.com/LuisAhumadaMartens/alumbra/issues/31#issuecomment-6555064887)
- [ADR-0001](0001-record-architecture-decisions.md), [ADR-0009](0009-toolchain.md),
  [ADR-0012](0012-repository-topology-and-packaging.md), [ADR-0015](0015-threading-and-task-runners.md),
  [ADR-0016](0016-penumbra-port-contract.md), [ADR-0020](0020-observability-logging-tracing-qoe-metrics.md)
- [Process and current CI](../process.md#continuous-integration), [security policy](../../SECURITY.md)
- [LLVM: libFuzzer](https://llvm.org/docs/LibFuzzer.html),
  [Clang: MemorySanitizer](https://clang.llvm.org/docs/MemorySanitizer.html)
- [ClusterFuzzLite: GitHub Actions integration](https://google.github.io/clusterfuzzlite/running-clusterfuzzlite/github-actions/),
  [OSS-Fuzz: accepting new projects](https://google.github.io/oss-fuzz/getting-started/accepting-new-projects/)
- [Playwright: browsers and media codecs](https://playwright.dev/docs/browsers),
  [Apple: testing with WebDriver in Safari](https://developer.apple.com/documentation/webkit/testing-with-webdriver-in-safari)
