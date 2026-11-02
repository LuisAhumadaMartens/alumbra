import argparse
import pathlib
import sys

import emitters
import model
import validate

ROOT = validate.ROOT

PROFILES = {
    "umbra": {
        "schema": "settings/schema",
        "emitter": "cpp",
        "options": {
            "namespace": "umbra::settings",
            "include_name": "umbra/settings/settings_keys.h",
            "header_path": "umbra/settings/internal_include/umbra/settings/"
                           "settings_keys.h",
            "source_path": "umbra/settings/settings_registry.cc",
            "platform_path": "umbra/settings/internal_include/umbra/settings/"
                             "platform.h",
        },
    },
    "test_all_types": {
        "schema": "tools/settings/fixtures/valid",
        "emitter": "cpp",
        "options": {
            "namespace": "umbra::settings::all_types",
            "include_name": "umbra/settings/testing/all_types_keys.h",
            "header_path": "test/umbra/settings/all_types/include/umbra/"
                           "settings/testing/all_types_keys.h",
            "source_path": "test/umbra/settings/all_types/"
                           "all_types_registry.cc",
        },
    },
}


def render(profiles=None, root=ROOT):
    outputs = {}
    for name in profiles or PROFILES:
        profile = PROFILES[name]
        emitter = emitters.EMITTERS[profile["emitter"]](profile["options"])
        for path, content in emitter.render(
                model.build(root / profile["schema"])).items():
            if path in outputs:
                raise emitters.EmitError(f"two outputs write {path}")
            outputs[path] = content
    return outputs


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--profile", action="append", choices=sorted(PROFILES))
    args = parser.parse_args(argv)
    try:
        outputs = render(args.profile)
        stale = []
        for relative, content in outputs.items():
            path = ROOT / relative
            if args.check:
                if not path.is_file() or path.read_text(encoding="utf-8") != content:
                    stale.append(relative)
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
        if stale:
            raise emitters.EmitError("stale generated files: " + ", ".join(stale))
    except (emitters.EmitError, validate.ValidationError, OSError) as error:
        print(f"settings generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
