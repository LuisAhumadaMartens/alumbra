import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import generate


class StageTest(unittest.TestCase):
    def test_infers_stage_from_the_file_name(self):
        self.assertEqual(generate.stage_of(pathlib.Path("a.vert.hlsl")), "vertex")
        self.assertEqual(generate.stage_of(pathlib.Path("a.frag.hlsl")), "fragment")

    def test_rejects_names_without_a_stage(self):
        with self.assertRaises(ValueError):
            generate.stage_of(pathlib.Path("a.hlsl"))


class CommandTest(unittest.TestCase):
    def test_builds_a_shadercross_command(self):
        result = generate.command(pathlib.Path("shadercross"),
                                  pathlib.Path("x.frag.hlsl"),
                                  pathlib.Path("x.frag.msl"), "msl")
        self.assertEqual(result, ["shadercross", "x.frag.hlsl", "-s", "HLSL",
                                  "-d", "MSL", "-t", "fragment", "-e", "main",
                                  "-o", "x.frag.msl"])


class DifferencesTest(unittest.TestCase):
    def test_reports_missing_changed_and_stale_outputs(self):
        with tempfile.TemporaryDirectory() as root:
            committed = pathlib.Path(root) / "committed"
            fresh = pathlib.Path(root) / "fresh"
            committed.mkdir()
            fresh.mkdir()
            (committed / "same.spv").write_bytes(b"a")
            (fresh / "same.spv").write_bytes(b"a")
            (committed / "changed.spv").write_bytes(b"a")
            (fresh / "changed.spv").write_bytes(b"b")
            (fresh / "missing.spv").write_bytes(b"a")
            (committed / "stale.msl").write_bytes(b"a")
            problems = generate.differences(
                ["same.spv", "changed.spv", "missing.spv"], committed, fresh)
        self.assertEqual(len(problems), 3)
        self.assertTrue(any("changed.spv" in p for p in problems))
        self.assertTrue(any("missing.spv" in p for p in problems))
        self.assertTrue(any("stale.msl" in p for p in problems))

    def test_the_committed_outputs_have_a_source_for_each_shader(self):
        names = {path.stem for path in generate.sources(generate.SHADER_DIR)}
        self.assertTrue(names)
        for name in names:
            for extension in generate.TARGETS:
                self.assertTrue(
                    (generate.GENERATED_DIR / f"{name}.{extension}").exists())


if __name__ == "__main__":
    unittest.main()
