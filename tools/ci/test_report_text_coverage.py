import pathlib
import tempfile
import unittest
from unittest import mock

import report_text_coverage


class ReportTextCoverageTest(unittest.TestCase):
    def test_writes_separate_summary_and_details_for_each_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            build = root / "build"
            profile_dir = root / "profiles"
            output_dir = root / "reports"
            profile_dir.mkdir()
            for target in report_text_coverage.TARGETS:
                target_profiles = profile_dir / target
                target_profiles.mkdir()
                (target_profiles / "run.profraw").write_bytes(b"profile")
                executable = build / "test" / "fuzz" / target
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_bytes(b"executable")
            def output(command):
                if command[1] == "merge":
                    return "merged"
                target = pathlib.Path(command[-1]).name
                kind = "details" if command[1] == "show" else "summary"
                return f"{kind} {target}"

            with mock.patch.object(report_text_coverage, "run",
                                   side_effect=output):
                with mock.patch("sys.argv", [
                    "report_text_coverage.py",
                    "--build-dir", str(build),
                    "--profile-dir", str(profile_dir),
                    "--output-dir", str(output_dir),
                    "--llvm-profdata", "llvm-profdata-21",
                    "--llvm-cov", "llvm-cov-21",
                ]):
                    report_text_coverage.main()
            for target in report_text_coverage.TARGETS:
                self.assertEqual(
                    (output_dir / f"{target}-summary.txt").read_text(),
                    f"summary {target}")
                self.assertEqual(
                    (output_dir / f"{target}.txt").read_text(),
                    f"details {target}")


if __name__ == "__main__":
    unittest.main()
