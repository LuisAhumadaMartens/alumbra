import json
import pathlib
import tempfile
import unittest
from unittest import mock

import emitters
import generate
import model

FIXTURES = pathlib.Path(__file__).parent / "fixtures"

RECORD = """schema_version = 1
release = "0.1.0"

[[settings]]
key = "{key}"
type = "{type}"
default = {default}
unit = ""
platforms = ["linux"]
audience = "integrator"
stability = "stable"
apply = "live"
since = "0.1.0"
description = "Test record."
{extra}
"""


def write_schema(directory, files):
    for name, records in files.items():
        (directory / f"{name}.toml").write_text(
            "".join(RECORD.format(**record) for record in records[:1]),
            encoding="utf-8")


class FakeEmitter(emitters.Emitter):
    name = "fake"

    def render(self, model):
        return {self.options["path"]: ",".join(s.key for s in model.settings)}


class GeneratorTest(unittest.TestCase):
    def test_committed_outputs_are_current(self):
        self.assertEqual(generate.main(["--check"]), 0)

    def test_output_is_deterministic(self):
        self.assertEqual(generate.render(), generate.render())

    def test_outputs_have_no_comments_and_fit_the_column_limit(self):
        for path, content in generate.render(["umbra"]).items():
            with self.subTest(path=path):
                self.assertNotIn("//", content.replace("://", ""))
                self.assertNotIn("/*", content)
                for line in content.splitlines():
                    self.assertTrue(len(line) <= 80 or line.lstrip().startswith(
                        ('"', ".")), line)

    def test_settings_are_sorted_for_binary_search(self):
        built = model.build(generate.ROOT / "settings/schema")
        keys = [setting.key for setting in built.settings]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual([alias for alias, _ in built.aliases],
                         sorted(alias for alias, _ in built.aliases))

    def test_cpp_emits_typed_keys_for_every_type(self):
        outputs = generate.render(["test_all_types"])
        header = next(content for path, content in outputs.items()
                      if path.endswith("_keys.h"))
        for declaration in (
                "Key<bool> kTestEnabled", "Key<std::int64_t> kTestCount",
                "Key<double> kTestRatioPercent", "Key<TestMode> kTestMode",
                "Key<std::string> kTestLabel",
                "Key<std::vector<std::string>> kTestTags",
                "Key<absl::Duration> kTestTimeoutMs",
                "Key<std::uint64_t> kTestCapacityBytes"):
            self.assertIn(declaration, header)

    def test_alias_and_deprecation_reach_the_registry(self):
        source = next(content for path, content in
                      generate.render(["test_all_types"]).items()
                      if path.endswith(".cc"))
        self.assertIn('.name = "test.old_timeout_ms"', source)
        self.assertIn(".deprecated = true", source)

    def render_reference_fixture(self):
        emitter = emitters.EMITTERS["markdown"](
            {"directory": "out", "index_name": "index.json",
             "route": "reference/settings"})
        return emitter.render(model.build(FIXTURES / "reference"))

    def test_reference_matches_golden_output(self):
        golden = FIXTURES / "reference_golden"
        outputs = self.render_reference_fixture()
        self.assertEqual(
            sorted(path.removeprefix("out/") + ".golden" for path in outputs),
            sorted(path.name for path in golden.iterdir()))
        for path, content in outputs.items():
            name = path.removeprefix("out/")
            with self.subTest(path=name):
                self.assertEqual(
                    (golden / (name + ".golden")).read_text(encoding="utf-8"),
                    content)

    def test_reference_lists_every_key_once_and_keeps_alias_anchors(self):
        outputs = self.render_reference_fixture()
        pages = "".join(content for path, content in outputs.items()
                        if path.endswith(".md"))
        for key in ("gadget.timeout_ms", "gadget.mode", "gadget.limit",
                    "gadget.legacy_limit", "widget.enabled"):
            self.assertEqual(pages.count(f'<a id="{key}"></a>'), 1)
        self.assertIn('<a id="gadget.request_timeout_ms"></a>', pages)
        self.assertIn("](gadget.md#gadget.limit) instead.", pages)
        index = json.loads(outputs["out/index.json"])
        self.assertEqual(index["aliases"]["gadget.request_timeout_ms"]["url"],
                         index["settings"]["gadget.timeout_ms"]["url"])

    def test_reference_output_is_visibly_marked_generated(self):
        for path, content in self.render_reference_fixture().items():
            if path.endswith(".md"):
                self.assertNotIn("<!--", content)
                self.assertIn("Generated file. Do not edit.", content)

    def test_emitters_are_pluggable(self):
        profiles = {"fake": {"schema": "tools/settings/fixtures/valid",
                             "emitter": "fake",
                             "options": {"path": "out/fake.txt"}}}
        with mock.patch.dict(emitters.EMITTERS, {"fake": FakeEmitter}), \
                mock.patch.dict(generate.PROFILES, profiles):
            outputs = generate.render(["fake"])
        self.assertEqual(list(outputs), ["out/fake.txt"])
        self.assertIn("test.count", outputs["out/fake.txt"])

    def test_colliding_output_paths_are_rejected(self):
        profiles = {"one": {"schema": "tools/settings/fixtures/valid",
                            "emitter": "fake", "options": {"path": "same"}},
                    "two": {"schema": "tools/settings/fixtures/valid",
                            "emitter": "fake", "options": {"path": "same"}}}
        with mock.patch.dict(emitters.EMITTERS, {"fake": FakeEmitter}), \
                mock.patch.dict(generate.PROFILES, profiles):
            with self.assertRaises(emitters.EmitError):
                generate.render(["one", "two"])

    def build_cpp(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            write_schema(root, files)
            emitter = emitters.EMITTERS["cpp"](
                generate.PROFILES["umbra"]["options"])
            return emitter.render(model.build(root))

    def test_identifier_collisions_are_rejected(self):
        files = {
            "a": [dict(key="a.b_c", type="boolean", default="true", extra="")],
            "a_b": [dict(key="a_b.c", type="boolean", default="true",
                         extra="")],
        }
        with self.assertRaisesRegex(emitters.EmitError, "map to kABC"):
            self.build_cpp(files)

    def enum_record(self, values):
        entries = ", ".join(f'{{ value = "{value}", description = "Doc." }}'
                            for value in values)
        return dict(key="a.mode", type="enum", default=f'"{values[0]}"',
                    extra=f"values = [{entries}]")

    def test_enum_value_collisions_and_bad_identifiers_are_rejected(self):
        with self.assertRaisesRegex(emitters.EmitError, "map to kAB"):
            self.build_cpp({"a": [self.enum_record(["a-b", "a_b"])]})
        with self.assertRaisesRegex(emitters.EmitError, "identifier"):
            self.build_cpp({"a": [self.enum_record(["a b"])]})
        outputs = self.build_cpp({"a": [self.enum_record(["fast-path", "x"])]})
        self.assertIn("kFastPath,", "".join(outputs.values()))

    def test_invalid_schema_is_reported_not_emitted(self):
        with tempfile.TemporaryDirectory() as directory:
            (pathlib.Path(directory) / "a.toml").write_text("not toml [")
            with self.assertRaises(Exception):
                model.build(directory)


if __name__ == "__main__":
    unittest.main()
