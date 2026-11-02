import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

import generate_blocklist as generate

ENTRY = """version = 1

[[entry]]
id = "vendor-h264-crash"
decoder = "vendor.h264.decoder"
codec = "h264"
port = "android"
os_min = "12"
os_max = "13.1"
device_model = "Model X"
gpu_vendor = "acme"
driver_min = "1.2"
driver_max = "1.4.9"
reason = "Green frames after a seek"
evidence = "https://github.com/LuisAhumadaMartens/alumbra/issues/1"
since = "0.1.0"

[[entry]]
id = "another-entry"
decoder = "other.decoder"
reason = "Hangs on start"
evidence = "https://example.org/report"
since = "0.2"
"""


def parse(text):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "blocklist.toml"
        path.write_text(text)
        return generate.load(path)


class GeneratorTest(unittest.TestCase):
    def test_shipped_file_loads(self):
        generate.load(generate.ROOT / generate.SOURCE)

    def test_entries_are_sorted_and_rendered(self):
        records = parse(ENTRY)
        self.assertEqual([r["id"] for r in records],
                         ["another-entry", "vendor-h264-crash"])
        outputs = generate.render(records)
        table = outputs[generate.TABLE]
        self.assertIn("std::array<BlocklistEntry, 2> kDecoderBlocklist{{",
                      table)
        self.assertIn('.id = "vendor-h264-crash",', table)
        self.assertIn('.driver_max = "1.4.9",', table)
        page = outputs[generate.PAGE]
        self.assertIn("`vendor-h264-crash`", page)
        self.assertIn("Green frames after a seek", page)
        self.assertEqual(outputs, generate.render(list(records)))

    def test_empty_blocklist_renders_an_empty_table(self):
        outputs = generate.render(parse("version = 1\n"))
        self.assertIn("std::array<BlocklistEntry, 0> kDecoderBlocklist{};",
                      outputs[generate.TABLE])
        self.assertIn("No decoder is blocklisted yet.",
                      outputs[generate.PAGE])

    def test_comments_do_not_appear_in_generated_code(self):
        for content in generate.render(parse(ENTRY)).values():
            self.assertNotIn("//", content.replace("https://", ""))
            self.assertNotIn("/*", content)

    def test_rejects_invalid_blocklists(self):
        for invalid in (
            ENTRY.replace("version = 1", "version = 2", 1),
            ENTRY.replace('id = "another-entry"', 'id = "vendor-h264-crash"'),
            ENTRY.replace('id = "another-entry"', 'id = "Bad_Id"'),
            ENTRY.replace('codec = "h264"', 'codec = "mpeg2"'),
            ENTRY.replace('os_min = "12"', 'os_min = "14"'),
            ENTRY.replace('driver_max = "1.4.9"', 'driver_max = "one"'),
            ENTRY.replace("https://example.org/report", "ftp://example.org"),
            ENTRY.replace('reason = "Hangs on start"\n', ""),
            ENTRY.replace('reason = "Hangs on start"', 'reason = "a\\"b"'),
            ENTRY + 'unknown = "x"\n',
            ENTRY.replace('since = "0.2"', "since = 2"),
        ):
            with self.subTest(invalid=invalid[-120:]):
                with self.assertRaises(ValueError):
                    parse(invalid)

    def test_check_detects_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "blocklist.toml"
            source.write_text(ENTRY)
            base = ["generate_blocklist.py", "--source", str(source),
                    "--root", str(root)]

            def run(*extra):
                with mock.patch.object(sys, "argv", base + list(extra)), \
                        contextlib.redirect_stderr(io.StringIO()):
                    return generate.main()

            self.assertEqual(run("--check"), 1)
            self.assertEqual(run(), 0)
            self.assertEqual(run("--check"), 0)
            table = root / generate.TABLE
            table.write_text(table.read_text() + "\n")
            self.assertEqual(run("--check"), 1)
            source.write_text(ENTRY.replace("Hangs on start", "Hangs"))
            self.assertEqual(run(), 0)
            source.write_text(ENTRY)
            self.assertEqual(run("--check"), 1)


if __name__ == "__main__":
    unittest.main()
