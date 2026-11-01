import unittest

from require_ci_jobs import validation_error


class RequiredJobsTest(unittest.TestCase):
    def test_relevant_changes_require_successful_build_and_sanitizers(self):
        self.assertEqual(
            validation_error("success success success", "true",
                             "success", "success"),
            "")
        self.assertNotEqual(
            validation_error("success success success", "true",
                             "success", "skipped"),
            "")
        self.assertNotEqual(
            validation_error("success success success", "true",
                             "skipped", "success"),
            "")

    def test_documentation_only_changes_require_explicit_skips(self):
        self.assertEqual(
            validation_error("success success success", "false",
                             "skipped", "skipped"),
            "")
        self.assertNotEqual(
            validation_error("success success success", "false",
                             "success", "skipped"),
            "")
        self.assertNotEqual(
            validation_error("success success success", "false",
                             "skipped", "success"),
            "")

    def test_foundation_jobs_and_classifier_fail_closed(self):
        for results in ("", "success success skipped", "success failure success",
                        "success cancelled success"):
            with self.subTest(results=results):
                self.assertNotEqual(
                    validation_error(results, "false", "skipped", "skipped"),
                    "")
        self.assertNotEqual(
            validation_error("success success success", "", "skipped",
                             "skipped"),
            "")


if __name__ == "__main__":
    unittest.main()
