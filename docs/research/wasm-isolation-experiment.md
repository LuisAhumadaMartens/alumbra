# WASM isolation experiment

**Status:** browser smoke implemented; CI execution and device matrix pending.

The Chromium smoke test serves pages without isolation headers and with COOP/COEP `require-corp` headers. It asserts
that the loader selects the single-threaded artefact for the first page and the threaded artefact when
`crossOriginIsolated` and `SharedArrayBuffer` are available. The test is wired into the Linux CI job. The local macOS
sandbox blocks Chromium's Mach port rendezvous, so the browser assertion has not yet run successfully.

The remaining experiment requires a physical phone, a physical TV browser and browser installations that are not
available in the build environment. No device measurements or claims are recorded here. In particular, the 64 MiB
initial and 512 MiB maximum heaps are build limits awaiting validation, and the four-worker pool has no long-task,
audio-underrun or size measurements. The engine has no web audio ring or `MessagePort` PCM path yet; #149 must
provide those before those paths can be compared.

| Environment | Isolation headers | `SharedArrayBuffer` | Loader result | IMA / iframe result |
|-------------|-------------------|--------------------|---------------|---------------------|
| Headless Chromium in CI | absent | unavailable | automated assertion pending | not measured |
| Headless Chromium in CI | COOP `same-origin`, COEP `require-corp` | available | automated assertion pending | not measured |
| Desktop Chrome, Edge, Firefox and Safari | pending | pending | pending | pending |
| iPhone and iPad Safari | pending | pending | pending | pending |
| Android Chrome phone | pending | pending | pending | pending |
| Google TV or Android TV browser | pending | pending | pending | pending |

Before release, record long-task duration, audio underruns and compressed/uncompressed artefact sizes on one phone
and one TV. Run the isolation and IMA iframe checks across the listed browsers. Check `credentialless` and
`Document-Isolation-Policy` separately where supported; this smoke test covers COOP/COEP with `require-corp` only.
