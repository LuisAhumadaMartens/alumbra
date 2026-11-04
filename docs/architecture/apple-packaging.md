# Apple binary packaging

The Apple packaging workflow builds the portable engine and its experimental C ABI for iOS and tvOS. The four
default slices are `ios-arm64`, `ios-simulator-arm64`, `tvos-arm64` and `tvos-simulator-arm64`, each at the
platform minimums in [ADR-0039](../adr/0039-compatibility-matrix-and-support-tiers.md). Simulator builds use
arm64, which is the architecture selected for the Apple simulator tier.

The build runs on macOS with Xcode 26.6 or newer and a bootstrapped vcpkg checkout. Run the complete release-shape
build with:

```sh
cmake --workflow --preset apple-xcframework
```

The result is `build/apple/Alumbra.xcframework`. Each framework slice contains the static Alumbra archive, the
public C ABI header, a Clang module map and `PrivacyInfo.xcprivacy`. The archive includes the portable engine and
its static core dependencies. The Apple playback port, Swift API, SwiftUI UI product and IMA provider are separate
work items; this package exposes the experimental C ABI defined by [ADR-0038](../adr/0038-sdk-bindings-through-a-public-c-abi.md).

The slice configure presets are also available individually for local work: `apple-ios-arm64`,
`apple-ios-simulator-arm64`, `apple-tvos-arm64` and `apple-tvos-simulator-arm64`. Set
`ALUMBRA_APPLE_SLICES` to a semicolon-separated subset when building the workflow. `ALUMBRA_APPLE_CATALYST` is
reserved for the opt-in Catalyst slice; it fails with a clear message until Catalyst dependency triplets have
licence and build coverage.

`tools/apple/verify_xcframework.py` checks the platform, architecture and minimum OS recorded in each built
archive, and verifies the module map and privacy manifest. The same workflow builds a small SwiftPM consumer for
each requested target triple. The consumer uses a local-path binary target so developers can test a build without
publishing an archive. Release automation creates the URL-and-checksum manifest in the distribution repository,
as required by [ADR-0012](../adr/0012-repository-topology-and-packaging.md).

The Apple build selects only the manifest's core dependencies: Abseil, SheenBidi and libunibreak. It does not
select the desktop feature or FFmpeg. Their notices and the applicable licence decisions remain in
[`THIRD_PARTY.md`](../../THIRD_PARTY.md).
