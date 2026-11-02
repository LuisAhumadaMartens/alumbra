import pathlib
import tempfile
import unittest

import check_binary_strings


class CheckBinaryStringsTest(unittest.TestCase):
    def run_check(self, content, *arguments):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "binary"
            path.write_bytes(content)
            return check_binary_strings.main([str(path), *arguments])

    def test_absent_strings_pass_when_missing(self):
        self.assertEqual(
            self.run_check(b"\0main\0other\0", "--absent-categories"), 0)

    def test_category_string_is_found_only_as_a_whole_string(self):
        self.assertEqual(
            self.run_check(b"\0network\0.text\0", "--absent-categories"), 0)
        self.assertEqual(
            self.run_check(b"\0main\0net\0", "--absent-categories"), 1)

    def test_present_strings_are_required(self):
        self.assertEqual(
            self.run_check(b"\0scope_name\0", "--present", "scope_name"), 0)
        self.assertEqual(
            self.run_check(b"\0other\0", "--present", "scope_name"), 1)

    def test_explicit_absent_string_fails_when_found(self):
        self.assertEqual(
            self.run_check(b"\0scope_name\0", "--absent", "scope_name"), 1)


if __name__ == "__main__":
    unittest.main()
