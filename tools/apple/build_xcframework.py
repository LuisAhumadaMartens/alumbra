import argparse
import os
import pathlib
import shutil
import subprocess


SLICES = {
    "ios-arm64": ("apple-ios-arm64", "iphoneos", "ios", "arm64"),
    "ios-simulator-arm64": (
        "apple-ios-simulator-arm64",
        "iphonesimulator",
        "ios-simulator",
        "arm64",
    ),
    "tvos-arm64": ("apple-tvos-arm64", "appletvos", "tvos", "arm64"),
    "tvos-simulator-arm64": (
        "apple-tvos-simulator-arm64",
        "appletvsimulator",
        "tvos-simulator",
        "arm64",
    ),
}


def run(command, cwd, environment=None):
    subprocess.run(command, cwd=cwd, env=environment, check=True)


def create_framework(source, build_root, slice_name, preset, sdk, platform, arch):
    build_dir = build_root / preset
    run(["cmake", "--preset", preset], source)
    run(["cmake", "--build", "--preset", preset, "--target", "umbra_c_api"], source)

    archives = sorted(
        path
        for path in build_dir.rglob("*.a")
        if "debug" not in path.parts
        and ("vcpkg_installed" in path.parts or path.parent.name != "CMakeFiles")
    )
    archives = [path for path in archives if "install" not in path.parts]
    if not archives:
        raise RuntimeError(f"No static libraries were built for {slice_name}")

    slice_root = build_root / "framework-slices" / slice_name
    framework = slice_root / "Alumbra.framework"
    if slice_root.exists():
        shutil.rmtree(slice_root)
    (framework / "Headers").mkdir(parents=True)
    (framework / "Modules").mkdir()
    library = framework / "Alumbra"
    run(["xcrun", "libtool", "-static", "-o", str(library), *map(str, archives)], source)
    shutil.copy2(
        source / "umbra/c_api/include/umbra/c_api/c_api.h",
        framework / "Headers" / "c_api.h",
    )
    shutil.copy2(source / "tools/apple/Alumbra.h", framework / "Headers" / "Alumbra.h")
    shutil.copy2(source / "tools/apple/module.modulemap", framework / "Modules/module.modulemap")
    shutil.copy2(source / "tools/apple/PrivacyInfo.xcprivacy", framework / "PrivacyInfo.xcprivacy")
    (framework / "Info.plist").write_text(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\"><dict>"
        "<key>CFBundleIdentifier</key><string>org.alumbra.sdk</string>"
        "<key>CFBundleName</key><string>Alumbra</string>"
        "<key>CFBundlePackageType</key><string>FMWK</string>"
        "<key>MinimumOSVersion</key><string>17.0</string>"
        f"<key>AlumbraPlatform</key><string>{platform}</string>"
        f"<key>AlumbraSDK</key><string>{sdk}</string>"
        f"<key>AlumbraArchitecture</key><string>{arch}</string>"
        "</dict></plist>\n",
        encoding="utf-8",
    )
    return framework


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--build-root", type=pathlib.Path, required=True)
    parser.add_argument("--vcpkg-root", required=True)
    parser.add_argument("--slices", required=True)
    parser.add_argument("--catalyst", choices=("0", "1"), default="0")
    args = parser.parse_args()
    if not args.vcpkg_root or not pathlib.Path(args.vcpkg_root).is_dir():
        raise RuntimeError("VCPKG_ROOT must point to the bootstrapped vcpkg checkout")
    if args.catalyst == "1":
        raise RuntimeError("Mac Catalyst is not available until its vcpkg triplets are reviewed")
    slices = [value for value in args.slices.split(";") if value]
    unknown = sorted(set(slices) - set(SLICES))
    if unknown:
        raise RuntimeError(f"Unsupported Apple slices: {', '.join(unknown)}")

    args.build_root.mkdir(parents=True, exist_ok=True)
    frameworks = []
    for slice_name in slices:
        preset, sdk, platform, arch = SLICES[slice_name]
        framework = create_framework(
            args.source, args.build_root, slice_name, preset, sdk, platform, arch
        )
        frameworks.extend(["-framework", str(framework)])

    xcframework = args.build_root / "Alumbra.xcframework"
    if xcframework.exists():
        shutil.rmtree(xcframework)
    run(["xcodebuild", "-create-xcframework", *frameworks, "-output", str(xcframework)], args.source)
    consumer = args.build_root / "swiftpm-consumer"
    if consumer.exists():
        shutil.rmtree(consumer)
    shutil.copytree(args.source / "tools/apple/swiftpm-consumer", consumer)
    shutil.copytree(xcframework, consumer / "Frameworks/Alumbra.xcframework")
    swift_cache = args.build_root / "swiftpm-cache"
    swift_config = args.build_root / "swiftpm-config"
    swift_security = args.build_root / "swiftpm-security"
    module_cache = args.build_root / "swift-module-cache"
    for directory in (swift_cache, swift_config, swift_security, module_cache):
        directory.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["CLANG_MODULE_CACHE_PATH"] = str(module_cache)
    triples = {
        "ios-arm64": "arm64-apple-ios17.0",
        "ios-simulator-arm64": "arm64-apple-ios17.0-simulator",
        "tvos-arm64": "arm64-apple-tvos17.0",
        "tvos-simulator-arm64": "arm64-apple-tvos17.0-simulator",
    }
    sdk_names = {
        "ios-arm64": "iphoneos",
        "ios-simulator-arm64": "iphonesimulator",
        "tvos-arm64": "appletvos",
        "tvos-simulator-arm64": "appletvsimulator",
    }
    for slice_name in slices:
        sdk_path = subprocess.check_output(
            ["xcrun", "--sdk", sdk_names[slice_name], "--show-sdk-path"],
            cwd=args.source,
            text=True,
        ).strip()
        run(
            [
                "swift",
                "build",
                "--cache-path",
                str(swift_cache),
                "--config-path",
                str(swift_config),
                "--security-path",
                str(swift_security),
                "--disable-sandbox",
                "--package-path",
                str(consumer),
                "--triple",
                triples[slice_name],
                "--sdk",
                sdk_path,
                "--scratch-path",
                str(consumer / "build" / slice_name),
            ],
            args.source,
            environment,
        )


if __name__ == "__main__":
    main()
