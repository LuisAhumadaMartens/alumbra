from pathlib import Path
import tempfile
import unittest

import generate

HEADER = (generate.ROOT / generate.HEADER).read_text()


def write(directory, text):
    path = Path(directory) / "rows.toml"
    path.write_text(text)
    return path


class GeneratorTest(unittest.TestCase):
    def setUp(self):
        self.source = (generate.ROOT / generate.SOURCE).read_text()
        self.tests = generate.known_tests(generate.ROOT / generate.TEST_ROOT)

    def rows(self, text):
        with tempfile.TemporaryDirectory() as directory:
            return generate.load(write(directory, text))

    def test_committed_table_is_consistent_and_current(self):
        rows = self.rows(self.source)
        self.assertEqual(generate.verify(rows, HEADER, self.tests), [])
        page = (generate.ROOT / generate.OUTPUT).read_text()
        self.assertEqual(page, generate.render(rows))

    def test_every_public_member_has_a_row(self):
        rows = self.rows(self.source)
        trimmed = [row for row in rows if "Play" not in row.get("cpp", [])]
        errors = generate.verify(trimmed, HEADER, self.tests)
        self.assertEqual(errors, ["public member Play has no conformance row"])

    def test_a_member_without_a_row_fails(self):
        rows = self.rows(self.source)
        header = HEADER.replace(
            "  void Pause();", "  void Pause();\n  void Rewind();")
        errors = generate.verify(rows, header, self.tests)
        self.assertEqual(errors, ["public member Rewind has no conformance row"])

    def test_a_row_for_a_missing_member_fails(self):
        rows = self.rows(self.source.replace('cpp = ["Pause"]',
                                             'cpp = ["Stop"]'))
        errors = generate.verify(rows, HEADER, self.tests)
        self.assertIn("public member Pause has no conformance row", errors)
        self.assertIn("row names Stop, which is not a public member", errors)

    def test_an_implemented_row_needs_an_existing_test(self):
        rows = self.rows(self.source.replace(
            "PlayerTest.PauseAbortsAPendingPlay", "PlayerTest.Missing", 1))
        errors = generate.verify(rows, HEADER, self.tests)
        self.assertTrue(any("PlayerTest.Missing" in error for error in errors))

    def test_an_implemented_row_without_tests_is_rejected(self):
        text = self.source.replace(
            'tests = ["PlayerTest.PauseAbortsAPendingPlay"]\n\n[[row]]\n'
            'name = "paused"', 'tests = []\n\n[[row]]\nname = "paused"', 1)
        with self.assertRaises(ValueError):
            self.rows(text)

    def test_cpp_names_must_map_to_the_html_name(self):
        rows = self.rows(self.source.replace('cpp = ["network_state"]',
                                             'cpp = ["networkstate"]'))
        errors = generate.verify(rows, HEADER.replace(
            "network_state()", "networkstate()"), self.tests)
        self.assertTrue(any("do not map" in error for error in errors))

    def test_rejects_invalid_sources(self):
        for invalid in (
            self.source.replace('stability = "stable"', 'stability = "x"', 1),
            self.source.replace('status = "implemented"', 'status = "todo"', 1),
            self.source.replace('kind = "attribute"', 'kind = "field"', 1),
            self.source + "\n[[row]]\nname = \"src\"\nkind = \"attribute\"\n"
            "status = \"absent\"\n",
            self.source + "\n[[row]]\nname = \"x\"\nkind = \"method\"\n"
            "status = \"absent\"\ncpp = [\"Load\"]\n",
            self.source + "\n[[row]]\nunknown = 1\n",
            self.source.replace("version = 1", "version = 2", 1),
        ):
            with self.subTest(invalid=invalid[:80]):
                with self.assertRaises(ValueError):
                    self.rows(invalid)

    def test_binding_names_follow_the_mechanical_rule(self):
        self.assertEqual(generate.binding_name("Play"), "play")
        self.assertEqual(generate.binding_name("current_time"), "currentTime")
        self.assertEqual(generate.binding_name("set_current_time"),
                         "currentTime")
        self.assertEqual(generate.binding_name("FastSeek"), "fastSeek")


if __name__ == "__main__":
    unittest.main()
