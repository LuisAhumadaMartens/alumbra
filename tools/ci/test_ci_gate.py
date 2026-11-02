import unittest

from require_ci_jobs import (CXX_JOBS, FOUNDATION_JOBS, TOKEN_JOBS,
                             SITE_JOBS, reference_error, site_error,
                             validation_error)

FOUNDATION = " ".join("success" for _ in FOUNDATION_JOBS)


def results(jobs, value, **overrides):
    values = {job: value for job in jobs}
    values.update(overrides)
    return values


def check(foundation=FOUNDATION, cxx="true", cxx_results=None, tokens="true",
          token_results=None):
    return validation_error(
        foundation, cxx,
        results(CXX_JOBS, "success") if cxx_results is None else cxx_results,
        tokens,
        results(TOKEN_JOBS, "success") if token_results is None else token_results)


class RequiredJobsTest(unittest.TestCase):
    def test_relevant_changes_require_every_cxx_job_to_succeed(self):
        self.assertEqual(check(), "")
        for job in CXX_JOBS:
            for outcome in ("skipped", "failure", "cancelled", ""):
                with self.subTest(job=job, outcome=outcome):
                    self.assertNotEqual(
                        check(cxx_results=results(CXX_JOBS, "success", **{job: outcome})),
                        "")

    def test_fuzz_smoke_is_required_for_cxx_changes(self):
        self.assertIn("fuzz", CXX_JOBS)
        for outcome in ("skipped", "failure", "cancelled"):
            with self.subTest(outcome=outcome):
                self.assertIn(
                    "fuzz smoke",
                    check(cxx_results=results(CXX_JOBS, "success",
                                              fuzz=outcome)))

    def test_windows_engine_job_is_required_for_cxx_changes(self):
        self.assertIn("windows", CXX_JOBS)
        for outcome in ("skipped", "failure", "cancelled", ""):
            with self.subTest(outcome=outcome):
                self.assertIn(
                    "Windows engine",
                    check(cxx_results=results(CXX_JOBS, "success",
                                              windows=outcome)))

    def test_documentation_only_changes_require_explicit_skips(self):
        skipped = results(CXX_JOBS, "skipped")
        no_tokens = results(TOKEN_JOBS, "skipped")
        self.assertEqual(
            check(cxx="false", cxx_results=skipped, tokens="false",
                  token_results=no_tokens), "")
        for job in CXX_JOBS:
            with self.subTest(job=job):
                self.assertNotEqual(
                    check(cxx="false",
                          cxx_results=results(CXX_JOBS, "skipped", **{job: "success"}),
                          tokens="false", token_results=no_tokens),
                    "")

    def test_token_changes_require_every_token_job_to_succeed(self):
        for job in TOKEN_JOBS:
            for outcome in ("skipped", "failure", "cancelled", ""):
                with self.subTest(job=job, outcome=outcome):
                    self.assertIn(
                        "Design-token changes",
                        check(token_results=results(TOKEN_JOBS, "success", **{job: outcome})))

    def test_changes_without_token_inputs_require_explicit_token_skips(self):
        self.assertEqual(check(tokens="false", token_results=results(TOKEN_JOBS, "skipped")), "")
        for job in TOKEN_JOBS:
            with self.subTest(job=job):
                self.assertIn(
                    "may skip the Design-token jobs",
                    check(tokens="false",
                          token_results=results(TOKEN_JOBS, "skipped", **{job: "success"})))

    def test_foundation_jobs_and_classifier_fail_closed(self):
        for foundation in ("", "success success success success",
                           "success success success success success success",
                           "success success success success skipped",
                           "success failure success success success",
                           "success success success success cancelled"):
            with self.subTest(foundation=foundation):
                self.assertNotEqual(check(foundation=foundation), "")
        self.assertNotEqual(check(cxx=""), "")
        self.assertIn("valid Design-token decision", check(tokens=""))

    def test_conformance_validation_is_a_foundation_job(self):
        self.assertEqual(
            FOUNDATION_JOBS,
            ("changes", "actionlint", "settings", "licence", "conformance"))
        self.assertNotIn("conformance-remote", FOUNDATION_JOBS)
        self.assertIn(
            "conformance",
            check(foundation="success success success success failure"))


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


class SiteGateTest(unittest.TestCase):
    def test_site_changes_require_every_site_job_to_succeed(self):
        self.assertEqual(site_error("true", results(SITE_JOBS, "success")), "")
        for job in SITE_JOBS:
            for outcome in ("skipped", "failure", "cancelled", ""):
                with self.subTest(job=job, outcome=outcome):
                    self.assertNotEqual(
                        site_error("true", results(
                            SITE_JOBS, "success", **{job: outcome})), "")

    def test_unrelated_changes_require_explicit_site_skips(self):
        self.assertEqual(site_error("false", results(SITE_JOBS, "skipped")), "")
        for job in SITE_JOBS:
            with self.subTest(job=job):
                self.assertNotEqual(
                    site_error("false", results(
                        SITE_JOBS, "skipped", **{job: "success"})), "")

    def test_missing_decision_fails_closed(self):
        for decision in ("", "maybe", "TRUE"):
            with self.subTest(decision=decision):
                self.assertNotEqual(
                    site_error(decision, results(SITE_JOBS, "success")), "")


if __name__ == "__main__":
    unittest.main()
