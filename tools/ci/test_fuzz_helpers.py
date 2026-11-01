import contextlib
import io
import json
import os
import pathlib
import tempfile
import unittest
from unittest import mock

import run_fuzz_target
import summarize_fuzz

FAKE_LIBFUZZER = r'''#!/usr/bin/env python3
import json
import os
import pathlib
import sys

arguments = sys.argv[1:]
pathlib.Path(os.environ["FAKE_FUZZER_ARGS"]).write_text(json.dumps(arguments))
flags = dict(argument[1:].split("=", 1) for argument in arguments
             if argument.startswith("-"))
mode = os.environ.get("FAKE_FUZZER_MODE", "pass")
runs = int(os.environ.get("FAKE_FUZZER_RUNS", flags["runs"]))
findings = {
    "crash": ("==1==ERROR: AddressSanitizer: heap-buffer-overflow", "crash-", 1),
    "leak": ("==1==ERROR: LeakSanitizer: detected memory leaks", "leak-", 1),
    "oom": ("==1== ERROR: libFuzzer: out-of-memory (malloc(1))", "oom-", 1),
    "timeout": ("==1== ERROR: libFuzzer: timeout after 10 seconds",
                "timeout-", 70),
    "ubsan": ("a.cc:1:1: runtime error: signed integer overflow", "crash-", 1),
    "crash-without-reproducer": ("==1==ERROR: AddressSanitizer: SEGV", None, 1),
    "unrecognized": ("something else went wrong", None, 2),
}
code = 0
if mode in findings:
    text, prefix, code = findings[mode]
    print(text)
    if prefix:
        pathlib.Path(flags["artifact_prefix"] + prefix + "0123").write_bytes(
            b"input")
print(f"stat::number_of_executed_units: {runs}")
print("stat::average_exec_per_sec:     1234")
print("stat::new_units_added:          5")
print("stat::slowest_unit_time_sec:    0")
print("stat::peak_rss_mb:              48")
sys.exit(code)
'''


class RunFuzzTargetTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = pathlib.Path(temp.name)
        self.fuzzer = self.root / "fake_fuzzer"
        self.fuzzer.write_text(FAKE_LIBFUZZER, encoding="utf-8")
        self.fuzzer.chmod(0o755)
        self.corpus = self.root / "corpus"
        self.corpus.mkdir()
        (self.corpus / "seed").write_bytes(b"seed")
        self.regressions = self.root / "regressions"
        self.regressions.mkdir()
        (self.regressions / "case").write_bytes(b"case")
        self.dictionary = self.root / "target.dict"
        self.dictionary.write_text('token="x"\n', encoding="utf-8")
        self.reports = self.root / "reports"
        self.recorded = self.root / "arguments.json"
        self.environment = {"FAKE_FUZZER_ARGS": str(self.recorded),
                            "ALUMBRA_FUZZ_REPORT_DIR": str(self.reports)}

    def run_target(self, mode="pass", expect=None, extra=None,
                   corpora=None, environment=None):
        arguments = [
            "--name", "reader_fuzzer",
            "--executable", str(self.fuzzer),
            "--work-dir", str(self.root / "work"),
            "--runs", "50", "--seed", "7",
            "--rss-limit-mb", "2048", "--timeout", "10",
            "--dict", str(self.dictionary),
        ]
        for corpus in corpora or (self.corpus, self.regressions):
            arguments += ["--corpus", str(corpus)]
        if expect:
            arguments += ["--expect", expect]
        arguments += extra or []
        values = dict(self.environment, FAKE_FUZZER_MODE=mode)
        values.update(environment or {})
        output = io.StringIO()
        with (mock.patch.dict(os.environ, values, clear=False),
              contextlib.redirect_stdout(output),
              contextlib.redirect_stderr(output)):
            code = run_fuzz_target.main(arguments)
        return code, output.getvalue()

    def result(self):
        return json.loads(
            (self.reports / "reader_fuzzer" / "result.json").read_text(
                encoding="utf-8"))

    def test_passing_run_uses_the_fixed_budget_and_every_corpus(self):
        code, output = self.run_target()
        self.assertEqual(code, 0, output)
        arguments = json.loads(self.recorded.read_text(encoding="utf-8"))
        artifacts = self.reports / "reader_fuzzer"
        self.assertEqual(arguments[:7], [
            "-runs=50", "-seed=7", "-rss_limit_mb=2048", "-timeout=10",
            "-print_final_stats=1",
            f"-artifact_prefix={artifacts.resolve()}{os.sep}",
            f"-dict={self.dictionary.resolve()}",
        ])
        self.assertEqual(arguments[7:], [
            str((self.root / "work" / "reader_fuzzer" / "corpus").resolve()),
            str(self.corpus.resolve()), str(self.regressions.resolve()),
        ])
        result = self.result()
        self.assertTrue(result["passed"])
        self.assertEqual(result["seeds"], 2)
        self.assertEqual(result["stats"]["number_of_executed_units"], 50)
        self.assertEqual(result["stats"]["peak_rss_mb"], 48)
        self.assertEqual(result["budget"], {"runs": 50, "seed": 7,
                                            "rss_limit_mb": 2048,
                                            "timeout": 10})
        self.assertIn("passed: 50 runs", output)

    def test_every_finding_fails_and_keeps_its_reproducer(self):
        for mode, kind in (("crash", "crash"), ("ubsan", "crash"),
                           ("leak", "leak"), ("oom", "oom"),
                           ("timeout", "timeout")):
            with self.subTest(mode=mode):
                code, output = self.run_target(mode)
                self.assertEqual(code, 1)
                result = self.result()
                self.assertFalse(result["passed"])
                self.assertEqual(result["finding"], kind)
                reproducer = self.reports / "reader_fuzzer" / f"{kind}-0123"
                self.assertEqual(result["artifacts"], [str(reproducer)])
                self.assertIn(f"Reproduce with: {self.fuzzer} {reproducer}",
                              output)
                self.assertIn("stat::peak_rss_mb", output)

    def test_unrecognized_failures_and_short_runs_fail_closed(self):
        code, output = self.run_target("unrecognized")
        self.assertEqual(code, 1)
        self.assertIn("without a recognized finding", output)
        code, output = self.run_target(environment={"FAKE_FUZZER_RUNS": "10"})
        self.assertEqual(code, 1)
        self.assertIn("executed 10 of the 50 budgeted runs", output)

    def test_missing_or_empty_corpus_fails(self):
        empty = self.root / "empty"
        empty.mkdir()
        code, output = self.run_target(corpora=[empty])
        self.assertEqual(code, 1)
        self.assertIn("the seed corpus is empty", output)
        code, output = self.run_target(corpora=[self.root / "missing"])
        self.assertEqual(code, 1)
        self.assertIn("does not exist", output)
        self.assertFalse(self.recorded.exists())

    def test_canary_requires_the_expected_finding_and_reproducer(self):
        code, output = self.run_target("crash", expect="crash")
        self.assertEqual(code, 0, output)
        self.assertIn("failed as expected with a crash", output)
        for mode, message in (
                ("pass", "expected a crash but found none"),
                ("leak", "expected a crash but found leak"),
                ("crash-without-reproducer", "saved no crash- reproducer")):
            with self.subTest(mode=mode):
                code, output = self.run_target(mode, expect="crash")
                self.assertEqual(code, 1)
                self.assertIn(message, output)

    def test_reports_default_to_the_work_directory(self):
        values = {key: value for key, value in self.environment.items()
                  if key != "ALUMBRA_FUZZ_REPORT_DIR"}
        with mock.patch.dict(os.environ, {}, clear=False):
            os.environ.pop("ALUMBRA_FUZZ_REPORT_DIR", None)
            self.environment = values
            code, output = self.run_target()
        self.assertEqual(code, 0, output)
        self.assertTrue(
            (self.root / "work" / "reports" / "reader_fuzzer"
             / "result.json").is_file())

    def test_budgets_must_be_positive_integers(self):
        for value in ("0", "-1", "ten"):
            with self.subTest(value=value):
                with (self.assertRaises(SystemExit),
                      contextlib.redirect_stderr(io.StringIO())):
                    run_fuzz_target.parse_arguments([
                        "--name", "x", "--executable", "x", "--corpus", "x",
                        "--work-dir", "x", "--runs", value, "--seed", "1",
                        "--rss-limit-mb", "1", "--timeout", "1"])


class SummarizeFuzzTest(unittest.TestCase):
    def test_renders_budget_results_and_reproducers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            budget = {"runs": 50, "seed": 7, "rss_limit_mb": 2048,
                      "timeout": 10}
            for name, passed, artifacts in (
                    ("a_fuzzer", True, []),
                    ("b_fuzzer", False, [str(root / "b" / "crash-01")])):
                (root / name).mkdir()
                (root / name / "result.json").write_text(json.dumps({
                    "name": name, "passed": passed, "budget": budget,
                    "verdict": "passed" if passed else "found a crash",
                    "seconds": 1.5, "artifacts": artifacts,
                    "stats": {"number_of_executed_units": 50,
                              "average_exec_per_sec": 9,
                              "peak_rss_mb": 40}}), encoding="utf-8")
            text = summarize_fuzz.render("Fuzz", root)
        self.assertIn("`-runs=50` `-seed=7` `-rss_limit_mb=2048` "
                      "`-timeout=10`", text)
        self.assertIn("| `a_fuzzer` | ✅ passed | 50 | 1.5 | 9 | 0 | 0 | 40 "
                      "| none |", text)
        self.assertIn("| `b_fuzzer` | ❌ found a crash |", text)
        self.assertIn("`crash-01`", text)

    def test_reports_missing_results(self):
        with tempfile.TemporaryDirectory() as temp:
            self.assertIn("No fuzz reports were produced.",
                          summarize_fuzz.render("Fuzz", pathlib.Path(temp)))


if __name__ == "__main__":
    unittest.main()
