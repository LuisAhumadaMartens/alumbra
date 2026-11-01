import contextlib
import io
import os
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

import run_sanitizer_canary


class SanitizerCanaryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.executable = self.root / "canary"

    def write_canary(self, output, exit_code):
        self.executable.write_text(
            "#!/usr/bin/env python3\n"
            "import sys\n"
            f"sys.stderr.write({output!r})\n"
            f"raise SystemExit({exit_code})\n",
            encoding="utf-8")
        self.executable.chmod(0o755)

    def run_canary(self, diagnostic, report_dir=None):
        arguments = [
            "run_sanitizer_canary.py",
            "--executable", str(self.executable),
            "--case", "heap-buffer-overflow",
            "--diagnostic", diagnostic,
        ]
        environment = {"ALUMBRA_CANARY_REPORT_DIR": report_dir} \
            if report_dir else {}
        output = io.StringIO()
        with (mock.patch.object(sys, "argv", arguments),
              mock.patch.dict(os.environ, environment, clear=False),
              contextlib.redirect_stdout(output),
              contextlib.redirect_stderr(output)):
            result = run_sanitizer_canary.main()
        return result, output.getvalue()

    def test_expected_sanitizer_failure_is_reported_and_saved(self):
        self.write_canary(
            "ERROR: AddressSanitizer: heap-buffer-overflow\n", 1)
        reports = self.root / "reports"
        result, output = self.run_canary(
            "ERROR: AddressSanitizer: heap-buffer-overflow", str(reports))
        self.assertEqual(result, 0)
        self.assertIn("produced the expected diagnostic", output)
        self.assertEqual(
            (reports / "heap-buffer-overflow.log").read_text(
                encoding="utf-8"),
            "ERROR: AddressSanitizer: heap-buffer-overflow\n")

    def test_unexpected_success_does_not_pass(self):
        self.write_canary("", 0)
        result, output = self.run_canary("heap-buffer-overflow")
        self.assertEqual(result, 1)
        self.assertIn("unexpectedly succeeded", output)

    def test_wrong_sanitizer_diagnostic_does_not_pass(self):
        self.write_canary("AddressSanitizer: DEADLYSIGNAL\n", 1)
        result, output = self.run_canary("heap-buffer-overflow")
        self.assertEqual(result, 1)
        self.assertIn("without the expected diagnostic", output)

    def test_suppression_files_are_empty_or_issue_linked(self):
        repository = pathlib.Path(__file__).resolve().parents[2]
        suppressions = sorted((repository / "test/sanitizers").glob("*.supp"))
        self.assertTrue(suppressions)
        for suppression in suppressions:
            for entry in suppression.read_text(encoding="utf-8").splitlines():
                if entry.strip():
                    self.assertRegex(
                        entry,
                        r"(?:https://github\.com/[^/\s]+/[^/\s]+/issues/\d+|#\d+)")


if __name__ == "__main__":
    unittest.main()
