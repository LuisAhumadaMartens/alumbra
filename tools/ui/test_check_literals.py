import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_literals


class ViolationTest(unittest.TestCase):
    def test_accepts_token_reads_and_structural_numbers(self):
        text = "float half = style.tight / 2;\nconst float a = 0.5F + 1;\n"
        self.assertEqual(check_literals.violations_in(text, "x.cc"), [])

    def test_rejects_pixel_sizes(self):
        problems = check_literals.violations_in("float pad = 16.0F;\n", "x.cc")
        self.assertEqual(len(problems), 1)
        self.assertIn("16.0", problems[0])

    def test_rejects_integer_sizes_and_decimals(self):
        self.assertEqual(len(check_literals.violations_in("int w = 44;\n", "x.cc")), 1)
        self.assertEqual(len(check_literals.violations_in("float o = 0.48F;\n", "x.cc")), 1)

    def test_rejects_hex_colours_in_either_form(self):
        self.assertEqual(len(check_literals.violations_in('auto c = "#0ea5e9";\n', "x.cc")), 1)
        self.assertEqual(len(check_literals.violations_in("auto c = 0xff0ea5e9;\n", "x.cc")), 1)

    def test_rejects_colours_built_from_literals(self):
        text = "Rgba c{0.1F, 0.2F, 0.3F, 1.0F};\n"
        self.assertTrue(check_literals.violations_in(text, "x.cc"))
        self.assertTrue(check_literals.violations_in("auto c = Srgb(1, 1, 1);\n", "x.cc"))

    def test_ignores_includes_and_string_contents(self):
        text = '#include "a/b2.h"\nstd::string s = "icon 16";\n'
        self.assertEqual(check_literals.violations_in(text, "x.cc"), [])


class RepositoryTest(unittest.TestCase):
    def test_the_renderer_style_code_passes(self):
        self.assertEqual(check_literals.check(check_literals.ROOT), [])

    def test_missing_files_fail(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(len(check_literals.check(pathlib.Path(directory))), 4)


if __name__ == "__main__":
    unittest.main()
