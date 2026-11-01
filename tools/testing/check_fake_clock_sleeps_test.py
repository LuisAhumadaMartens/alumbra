import pathlib
import tempfile
import unittest

from check_fake_clock_sleeps import violations


class FakeClockSleepLintTest(unittest.TestCase):
    def scan(self, files):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve()
            for name, text in files.items():
                (root / name).write_text(text, encoding="utf-8")
            return list(violations(root, []))

    def test_sleep_spellings_and_multiline(self):
        for call in ("::sleep", "clock_nanosleep", "std::this_thread::sleep_for",
                     "std::this_thread::sleep_until", "absl::SleepFor",
                     "usleep", "nanosleep", "Sleep"):
            with self.subTest(call=call):
                found = self.scan({"test.cc": f"FakeTickClock clock;\n{call}\n(1);\n"})
                self.assertEqual(len(found), 1)
                self.assertIn("test.cc:2:", found[0])

    def test_transitive_fixture_and_cycle(self):
        found = self.scan({
            "test.cc": '#include "fixture.hpp"\n::sleep(1);\n',
            "fixture.hpp": '#include "clock.hh"\n',
            "clock.hh": '#include "fixture.hpp"\nTestTaskEnvironment environment;\n',
        })
        self.assertEqual(len(found), 1)
        self.assertIn("test.cc:2:", found[0])

    def test_non_fake_clock_sleep_is_allowed(self):
        self.assertEqual(self.scan({"test.cc": "::sleep(1);\n"}), [])

    def test_fixture_in_another_scan_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve()
            tests = root / "test"
            support = root / "support"
            tests.mkdir()
            support.mkdir()
            (tests / "test.cc").write_text(
                '#include "fixture.hxx"\nclock_nanosleep(1);\n', encoding="utf-8")
            (support / "fixture.hxx").write_text(
                "TestTaskEnvironment environment;\n", encoding="utf-8")
            found = list(violations([tests, support], []))
            self.assertEqual(len(found), 1)
            self.assertIn("test.cc:2:", found[0])

    def test_fake_clock_without_sleep_is_allowed(self):
        self.assertEqual(self.scan({"test.cc": "FakeTickClock clock;\n"}), [])


if __name__ == "__main__":
    unittest.main()
