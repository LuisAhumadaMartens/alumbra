from pathlib import Path
import tempfile
import unittest

import generate


class GeneratorTest(unittest.TestCase):
    def test_accepts_category_floor(self):
        source = (generate.ROOT / "errors/catalogue.toml").read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "floor.toml"
            path.write_text(source.replace("code = 1001", "code = 1000", 1))
            self.assertEqual(generate.load(path)[0]["code"], 1000)

    def test_outputs_cover_same_codes(self):
        records = generate.load(generate.ROOT / "errors/catalogue.toml")
        outputs = generate.render(records)
        self.assertEqual(len(outputs), 6)
        for content in outputs.values():
            for record in records:
                self.assertIn(str(record["code"]), content)
        self.assertEqual(outputs, generate.render(list(records)))

    def test_rejects_invalid_catalogue(self):
        source = (generate.ROOT / "errors/catalogue.toml").read_text()
        for invalid in (
            source.replace("code = 1001", "code = 2009", 1),
            source.replace("code = 1002", "code = 1001", 1),
            source.replace('severity = "fatal"', 'severity = "warning"', 1),
            source.replace("retryable = true", "retryable = 1", 1),
            source.replace("media_error = 2", "media_error = 5", 1),
            source.replace('canonical = "Unavailable"', 'canonical = "OK"', 1),
            source.replace('name = "NetworkTimeout"', 'name = "bad_name"', 1),
            source.replace('description = "Network request timed out"',
                           'description = "https://secret"', 1),
            source + "\nunknown = true\n",
        ):
            with self.subTest(invalid=invalid[:100]):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "invalid.toml"
                    path.write_text(invalid)
                    with self.assertRaises(ValueError):
                        generate.load(path)


if __name__ == "__main__":
    unittest.main()
