import unittest

from require_ci_jobs import CXX_JOBS, FOUNDATION_JOBS, validation_error

FOUNDATION = " ".join("success" for _ in FOUNDATION_JOBS)


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

    def test_fuzz_smoke_is_required_for_cxx_changes(self):
        self.assertIn("fuzz", CXX_JOBS)
        for outcome in ("skipped", "failure", "cancelled"):
            with self.subTest(outcome=outcome):
                self.assertIn(
                    "fuzz smoke",
                    validation_error(FOUNDATION, "true",
                                     results("success", fuzz=outcome)))

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
        for foundation in ("", "success success success",
                           "success success success success success",
                           "success success success skipped",
                           "success failure success success",
                           "success success success cancelled"):
            with self.subTest(foundation=foundation):
                self.assertNotEqual(
                    validation_error(foundation, "false", results("skipped")),
                    "")
        self.assertNotEqual(
            validation_error(FOUNDATION, "", results("skipped")), "")

    def test_conformance_validation_is_a_foundation_job(self):
        self.assertEqual(
            FOUNDATION_JOBS, ("changes", "actionlint", "settings", "conformance"))
        self.assertNotIn("conformance-remote", FOUNDATION_JOBS)
        self.assertIn(
            "conformance",
            validation_error("success success success failure", "false",
                             results("skipped")))


if __name__ == "__main__":
    unittest.main()
