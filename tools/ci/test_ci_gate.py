import unittest

from require_ci_jobs import CXX_JOBS, reference_error, validation_error

FOUNDATION = "success success success"


def results(value, **overrides):
    values = {job: value for job in CXX_JOBS}
    values.update(overrides)
    return values


class RequiredJobsTest(unittest.TestCase):
    def test_relevant_changes_require_every_cxx_job_to_succeed(self):
        self.assertEqual(
            validation_error(FOUNDATION, "true", results("success")), "")
        for job in CXX_JOBS:
            for outcome in ("skipped", "failure", "cancelled", ""):
                with self.subTest(job=job, outcome=outcome):
                    self.assertNotEqual(
                        validation_error(
                            FOUNDATION, "true",
                            results("success", **{job: outcome})),
                        "")

    def test_documentation_only_changes_require_explicit_skips(self):
        self.assertEqual(
            validation_error(FOUNDATION, "false", results("skipped")), "")
        for job in CXX_JOBS:
            with self.subTest(job=job):
                self.assertNotEqual(
                    validation_error(
                        FOUNDATION, "false",
                        results("skipped", **{job: "success"})),
                    "")

    def test_foundation_jobs_and_classifier_fail_closed(self):
        for foundation in ("", "success success skipped",
                           "success failure success",
                           "success cancelled success"):
            with self.subTest(foundation=foundation):
                self.assertNotEqual(
                    validation_error(foundation, "false", results("skipped")),
                    "")
        self.assertNotEqual(
            validation_error(FOUNDATION, "", results("skipped")), "")


class ReferenceGateTest(unittest.TestCase):
    def test_reference_inputs_require_a_successful_build(self):
        self.assertEqual(reference_error("true", "success"), "")
        for outcome in ("skipped", "failure", "cancelled", ""):
            with self.subTest(outcome=outcome):
                self.assertNotEqual(reference_error("true", outcome), "")

    def test_unrelated_changes_require_an_explicit_skip(self):
        self.assertEqual(reference_error("false", "skipped"), "")
        for outcome in ("success", "failure", "cancelled", ""):
            with self.subTest(outcome=outcome):
                self.assertNotEqual(reference_error("false", outcome), "")

    def test_missing_decision_fails_closed(self):
        for decision in ("", "yes", "TRUE"):
            with self.subTest(decision=decision):
                self.assertNotEqual(reference_error(decision, "success"), "")


if __name__ == "__main__":
    unittest.main()
