import os
import unittest

import manifest
import yaml_subset

try:
    import yaml
except ImportError:
    yaml = None

VALID = [
    "a: 1\nb: true\nc: false\nd: text with spaces, commas and (brackets)\n",
    "list:\n  - one\n  - \"two: quoted\"\n  - 3\n",
    ("items:\n  - id: first\n    tags: [a, b-c, \"d, e\"]\n    nested:\n      key: value\n"
     "  - id: second\n    tags: []\n"),
    "url: https://example.com/a?b=c&d=e#fragment\nhex: \"0ca3b2bd\"\nplain: 0ca3b2bd\n",
    "codec: hvc1.2.4.L123.B0\ntime: 00:00:23\nquote: \"a \\\"b\\\" \\\\ c\"\n",
    "key-with-dash: value\n",
    "seq:\n  -   spaced: item\n      other: 1\n",
    "unicode: \u00a9 Blender Foundation | mango.blender.org\n",
]
INVALID = {
    "a:\tb\n": "tabs",
    "a: b \n": "trailing whitespace",
    "# comment\na: b\n": "comments",
    "a: b # comment\n": "ambiguous",
    "---\na: b\n": "document markers",
    "a: b\na: c\n": "duplicate key",
    "a: yes\n": "implicit YAML type",
    "a: null\n": "implicit YAML type",
    "a: ~\n": "implicit YAML type",
    "a: 1.5\n": "implicit YAML type",
    "a: 0x1f\n": "implicit YAML type",
    "a: 012\n": "implicit YAML type",
    "a: 1_000\n": "implicit YAML type",
    "a: 2024-01-01\n": "implicit YAML type",
    "a: 1:20\n": "implicit YAML type",
    "a: .inf\n": "implicit YAML type",
    "a: &anchor b\n": "unsupported",
    "a: *alias\n": "unsupported",
    "a: |\n  text\n": "unsupported",
    "a: {b: c}\n": "unsupported",
    "a: 'single'\n": "unsupported",
    "a:\n": "indented block",
    "a: \"\n": "unterminated",
    "a: \"x\" y\n": "unexpected text",
    "a: \"\\n\"\n": "escapes",
    "a: [b, c\n": "same line",
    "a: [b,]\n": "empty value",
    "a: [b [c]]\n": "flow indicator",
    "a:\n- b\n": "indented block",
    "a:\n  b: c\n    d: e\n": "unexpected indentation",
    "a:\n  - b\n  c: d\n": "expected a sequence item",
    "a:\n  b: c\n  - d\n": "sequence item where a key",
    "A: b\n": "lowercase plain key",
    "on: b\n": "implicit YAML type",
    " a: b\n": "column 1",
    "": "empty",
    "a: b\u2028c\n": "line-separator",
    "a: -b\n": "indicator",
    "a: b:\n": "ambiguous",
}


def require_pyyaml():
    return os.environ.get("ALUMBRA_REQUIRE_PYYAML", "") == "1"


class YamlSubsetTest(unittest.TestCase):
    def test_structures_and_scalars(self):
        document = yaml_subset.load(VALID[2])
        self.assertEqual(document, {"items": [
            {"id": "first", "tags": ["a", "b-c", "d, e"], "nested": {"key": "value"}},
            {"id": "second", "tags": []},
        ]})
        self.assertEqual(yaml_subset.load(VALID[0]),
                         {"a": 1, "b": True, "c": False,
                          "d": "text with spaces, commas and (brackets)"})
        self.assertEqual(yaml_subset.load(VALID[4])["quote"], 'a "b" \\ c')
        self.assertEqual(yaml_subset.load(VALID[6]), {"seq": [{"spaced": "item", "other": 1}]})

    def test_rejected_constructs_name_the_line(self):
        for source, message in INVALID.items():
            with self.subTest(source=source):
                with self.assertRaisesRegex(yaml_subset.YamlSubsetError, message):
                    yaml_subset.load(source)
        with self.assertRaisesRegex(yaml_subset.YamlSubsetError, "^line 3: "):
            yaml_subset.load("a: b\nc: d\ne: yes\n")

    def test_matches_pyyaml(self):
        if yaml is None:
            if require_pyyaml():
                self.fail("PyYAML is required by ALUMBRA_REQUIRE_PYYAML=1")
            self.skipTest("PyYAML is not installed")
        sources = [manifest.DEFAULT_MANIFEST.read_text(encoding="utf-8"), *VALID]
        for source in sources:
            with self.subTest(source=source[:40]):
                self.assertEqual(yaml_subset.load(source), yaml.safe_load(source))
        for source, message in INVALID.items():
            if message != "implicit YAML type":
                continue
            with self.subTest(source=source):
                key, value = next(iter(yaml.safe_load(source).items()))
                self.assertFalse(isinstance(key, str) and isinstance(value, str))


if __name__ == "__main__":
    unittest.main()
