import pathlib
import plistlib
import re
import subprocess
import sys


expected = {
    "ios-arm64": ("ios", "", "arm64"),
    "ios-simulator-arm64": ("ios", "simulator", "arm64"),
    "tvos-arm64": ("tvos", "", "arm64"),
    "tvos-simulator-arm64": ("tvos", "simulator", "arm64"),
}
if len(sys.argv) == 3 and sys.argv[2].startswith("--slices="):
    requested = sys.argv[2].partition("=")[2].split(";")
    root = pathlib.Path(sys.argv[1])
else:
    root = pathlib.Path(sys.argv[1])
    requested = sys.argv[2:]
with (root / "Info.plist").open("rb") as file:
    plist = plistlib.load(file)
entries = {
    item["LibraryIdentifier"]: item for item in plist.get("AvailableLibraries", [])
}

for slice_name in requested:
    platform, variant, arch = expected[slice_name]
    matches = [
        (identifier, item)
        for identifier, item in entries.items()
        if item.get("SupportedPlatform") == platform
        and item.get("SupportedPlatformVariant", "") == variant
        and arch in item.get("SupportedArchitectures", [])
    ]
    if len(matches) != 1:
        raise SystemExit(f"Expected one {slice_name} entry in {root}, found {len(matches)}")
    identifier, item = matches[0]
    framework = root / identifier / item["LibraryPath"]
    binary = framework / "Alumbra"
    architecture_result = subprocess.run(
        ["xcrun", "lipo", "-info", str(binary)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if arch not in architecture_result:
        raise SystemExit(f"{slice_name} archive does not contain {arch}")
    result = subprocess.run(
        ["xcrun", "otool", "-l", "-arch", arch, str(binary)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    if "minos 17.0" not in result:
        raise SystemExit(f"{slice_name} has an unexpected minimum OS:\n{result}")
    platform_id = {
        ("ios", ""): "2",
        ("ios", "simulator"): "7",
        ("tvos", ""): "3",
        ("tvos", "simulator"): "8",
    }[(platform, variant)]
    platforms = set(re.findall(r"^\s*platform\s+(\S+)", result, re.MULTILINE))
    minimums = set(re.findall(r"^\s*minos\s+(\S+)", result, re.MULTILINE))
    if platforms != {platform_id}:
        raise SystemExit(f"{slice_name} has platform values {sorted(platforms)}")
    if minimums != {"17.0"}:
        raise SystemExit(f"{slice_name} has minimum OS values {sorted(minimums)}")

    privacy = framework / "PrivacyInfo.xcprivacy"
    if not privacy.is_file():
        raise SystemExit(f"{slice_name} has no privacy manifest")
    with privacy.open("rb") as file:
        privacy_data = plistlib.load(file)
    if privacy_data.get("NSPrivacyTracking") is not False:
        raise SystemExit(f"{slice_name} privacy manifest must disable tracking")
    module = framework / "Modules/module.modulemap"
    if not module.is_file():
        raise SystemExit(f"{slice_name} has no module map")
    if 'umbrella header "Alumbra.h"' not in module.read_text(encoding="utf-8"):
        raise SystemExit(f"{slice_name} module map does not export its umbrella header")
