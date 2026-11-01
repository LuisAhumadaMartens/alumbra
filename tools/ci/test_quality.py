import json
import pathlib
import tempfile
import unittest
from unittest import mock

import yaml

import check_cpp_policy
import quality


class QualityTest(unittest.TestCase):
    def test_all_extensions_and_generated_exclusions(self):
        for suffix in quality.EXTENSIONS:
            self.assertTrue(quality.is_source(f"umbra/base/a{suffix}"))
        for directory in quality.EXCLUDED:
            self.assertFalse(quality.is_source(f"{directory}/umbra/a.cc"))
            self.assertFalse(quality.is_source(f"umbra/{directory}/a.h"))
        self.assertTrue(quality.is_source("test/build/fixture/a.cc"))
        self.assertFalse(quality.is_source("umbra/base/CMakeLists.txt"))

    def test_tool_version_must_match_the_shared_pin(self):
        with mock.patch.dict("os.environ", {"LLVM_BIN_DIR": "/llvm/bin"}):
            with mock.patch("shutil.which", return_value="/llvm/bin/clang-tidy"):
                with mock.patch("subprocess.check_output",
                                return_value=f"LLVM version {quality.LLVM_MAJOR}.1.8"):
                    self.assertEqual(quality.llvm_tool("clang-tidy"),
                                     "/llvm/bin/clang-tidy")
                with mock.patch("subprocess.check_output",
                                return_value="LLVM version 20.1.8"):
                    with self.assertRaisesRegex(RuntimeError, "is required"):
                        quality.llvm_tool("clang-tidy")

    def test_database_covers_umbra_port_and_tests_not_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            paths = ["umbra/base/base.cc", "penumbra/desktop/desktop.cc",
                     "test/umbra_base_smoke_test.cc",
                     "build/debug/generated/a.cc",
                     "third_party/library/a.cc"]
            entries = [{"file": str(quality.ROOT / path),
                        "directory": str(quality.ROOT)} for path in paths]
            (build / "compile_commands.json").write_text(json.dumps(entries))
            selected = quality.translation_units(build)
            self.assertEqual([file.relative_to(quality.ROOT).as_posix()
                              for file, _ in selected], paths[:3])
            (build / "compile_commands.json").write_text("[]")
            with self.assertRaisesRegex(RuntimeError, "No Alumbra"):
                quality.translation_units(build)

    def test_no_namespace_comments_and_generated_header_diagnostics(self):
        tidy = yaml.safe_load((quality.ROOT / ".clang-tidy").read_text())
        self.assertIn("-google-readability-namespace-comments", tidy["Checks"])
        self.assertIn("bugprone-*", tidy["Checks"])
        formatter = list(yaml.safe_load_all(
            (quality.ROOT / ".clang-format").read_text()))[0]
        self.assertFalse(formatter["FixNamespaceComments"])


class FeaturePolicyTest(unittest.TestCase):
    def test_replay_preserves_flags_but_not_output_or_language(self):
        entry = {"command": "clang++ -I include -std=c++23 -o a.o -c a.cc",
                 "directory": "."}
        with mock.patch("check_cpp_policy.resource_arguments", return_value=[]):
            command = check_cpp_policy.compatibility_command(entry, "clang++-21")
        self.assertEqual(command[:4], ["clang++-21", "-I", "include", "a.cc"])
        self.assertIn("-std=c++20", command)
        self.assertIn("-pedantic-errors", command)
        self.assertNotIn("a.o", command)
        self.assertNotIn("-std=c++23", command)

    def test_banned_features_and_apple_floor_gate(self):
        for code in ["import std;", "export module sample;",
                     "[[assume(true)]];", "std::format(\"{}\", 1.5);",
                     "std::vformat(\"{}\", args);",
                     "#if __cplusplus > 202002L\nint x;\n#endif",
                     "#  elif __cplusplus >= 202302L",
                     "#ifdef __cpp_lib_expected",
                     "#if defined(__cpp_explicit_this_parameter)"]:
            self.assertTrue(check_cpp_policy.source_violations(code), code)
        for code in ['const char* s = "std::format";',
                     "// [[assume(true)]];", "int module = 0;",
                     "absl::StrFormat(\"%f\", 1.5);",
                     "static_assert(__cplusplus == 202002L);",
                     "#if defined(_LIBCPP_VERSION)",
                     "// #if __cplusplus > 202002L"]:
            self.assertFalse(check_cpp_policy.source_violations(code), code)


if __name__ == "__main__":
    unittest.main()
