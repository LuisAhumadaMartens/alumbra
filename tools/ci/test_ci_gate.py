import unittest

from require_ci_jobs import CXX_JOBS, SITE_JOBS, validation_error

FOUNDATION = "success success success"


def results(jobs, value, **overrides):
    values = {job: value for job in jobs}
    values.update(overrides)
    return values


def check(foundation=FOUNDATION, cxx_needed="true", cxx=None,
          site_needed="true", site=None):
    return validation_error(
        foundation, cxx_needed,
        results(CXX_JOBS, "success") if cxx is None else cxx,
        site_needed,
        results(SITE_JOBS, "success") if site is None else site)


class RequiredJobsTest(unittest.TestCase):
    def test_relevant_changes_require_every_classified_job_to_succeed(self):
        self.assertEqual(check(), "")
        for jobs, key in ((CXX_JOBS, "cxx"), (SITE_JOBS, "site")):
            for job in jobs:
                for outcome in ("skipped", "failure", "cancelled", ""):
                    with self.subTest(job=job, outcome=outcome):
                        self.assertNotEqual(
                            check(**{key: results(
                                jobs, "success", **{job: outcome})}),
                            "")

    def test_unclassified_changes_require_explicit_skips(self):
        skipped_cxx = results(CXX_JOBS, "skipped")
        skipped_site = results(SITE_JOBS, "skipped")
        self.assertEqual(check(cxx_needed="false", cxx=skipped_cxx), "")
        self.assertEqual(check(site_needed="false", site=skipped_site), "")
        self.assertEqual(
            check(cxx_needed="false", cxx=skipped_cxx, site_needed="false",
                  site=skipped_site),
            "")
        for jobs, key, needed in ((CXX_JOBS, "cxx", "cxx_needed"),
                                  (SITE_JOBS, "site", "site_needed")):
            for job in jobs:
                with self.subTest(job=job):
                    self.assertNotEqual(
                        check(**{needed: "false", key: results(
                            jobs, "skipped", **{job: "success"})}),
                        "")

    def test_foundation_jobs_and_classifier_fail_closed(self):
        skipped_cxx = results(CXX_JOBS, "skipped")
        for foundation in ("", "success success skipped",
                           "success failure success",
                           "success cancelled success"):
            with self.subTest(foundation=foundation):
                self.assertNotEqual(
                    check(foundation=foundation, cxx_needed="false",
                          cxx=skipped_cxx),
                    "")
        self.assertNotEqual(check(cxx_needed="", cxx=skipped_cxx), "")
        self.assertNotEqual(
            check(site_needed="", site=results(SITE_JOBS, "skipped")), "")
        self.assertNotEqual(check(site_needed="maybe"), "")


if __name__ == "__main__":
    unittest.main()
