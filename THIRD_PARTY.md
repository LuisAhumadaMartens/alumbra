# Third-party CI tooling

| Dependency | Version | SPDX licence | Use / source |
|------------|---------|--------------|--------------|
| PyYAML | 6.0.3 | MIT | CI-only YAML parsing and regression tests; pinned in [`tools/ci/requirements.txt`](tools/ci/requirements.txt). [Source and licence](https://github.com/yaml/pyyaml/tree/6.0.3). |
| libFuzzer (LLVM compiler-rt) | The LLVM major in [`tools/ci/llvm-major`](tools/ci/llvm-major) | Apache-2.0 WITH LLVM-exception | Test-only fuzzing runtime linked into the `fuzz` preset's fuzz executables; installed with the pinned Clang (`libclang-rt-<major>-dev` on Ubuntu). [Source and licence](https://github.com/llvm/llvm-project/tree/main/compiler-rt/lib/fuzzer). |

PyYAML is installed into an isolated CI environment, not linked into or shipped with Alumbra. Its distribution
includes its MIT licence; preserve that licence if redistributing the tool. MIT is automatically eligible
under [ADR-0014](docs/adr/0014-dependency-and-licensing-policy.md), which makes ADR-0010 precise.

libFuzzer ships with the pinned LLVM toolchain, like the sanitizer runtimes, and adds no package to
`vcpkg.json`. It is linked only into fuzz executables, which are never installed or distributed
(see [fuzz targets](test/fuzz/README.md)), so no redistribution condition applies. Apache-2.0 is
automatically eligible under ADR-0014, which asks for `WITH` exceptions to be reviewed; the LLVM exception
only grants extra permission to redistribute object code compiled from the runtime.

This records CI and test tooling, not a complete runtime dependency inventory. The existing runtime
dependency inventory and distribution review remain tracked by #40.
