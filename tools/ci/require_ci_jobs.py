import os
import sys

FOUNDATION_JOBS = ("changes", "actionlint", "settings", "licence", "conformance")
CXX_JOBS = ("build", "sanitizers", "fuzz", "format", "tidy")
TOKEN_JOBS = ("tokens", "tokens_swift", "tokens_validate")
SITE_JOBS = ("prose", "site")


def classified_error(needed, results, jobs, label, description, skippable):
    if needed == "true":
        if any(results.get(job) != "success" for job in jobs):
            return f"{label} changes require successful {description} jobs."
        return ""
    if needed == "false":
        if any(results.get(job) != "skipped" for job in jobs):
            return (
                f"Only classifier-approved {skippable} may skip "
                f"the {label} jobs.")
        return ""
    return f"The change classifier did not produce a valid {label} decision."


def validation_error(foundation_results, cxx_needed, cxx_results,
                     tokens_needed, token_results):
    results = foundation_results.split()
    if (len(results) != len(FOUNDATION_JOBS)
            or any(result != "success" for result in results)):
        return ("Every foundation CI job must succeed: "
                + ", ".join(FOUNDATION_JOBS) + ".")
    return (
        classified_error(cxx_needed, cxx_results, CXX_JOBS, "C++",
                         "build, sanitizer, fuzz smoke, formatting and lint",
                         "documentation changes")
        or classified_error(tokens_needed, token_results, TOKEN_JOBS,
                            "Design-token", "validation, drift, CSS, TypeScript, Kotlin and Swift",
                            "changes without design-token inputs"))


def site_error(site_needed, site_results):
    return classified_error(
        site_needed, site_results, SITE_JOBS, "documentation site",
        "prose and site build",
        "changes the classifier keeps away from the documentation site")


def reference_error(reference_needed, reference_result):
    if reference_needed == "true":
        if reference_result != "success":
            return ("Changes to public headers, their documentation or the "
                    "reference tooling require a successful C++ API "
                    "reference build.")
        return ""
    if reference_needed == "false":
        if reference_result != "skipped":
            return ("The C++ API reference job may skip only when the "
                    "classifier finds no reference input.")
        return ""
    return ("The change classifier did not produce a valid C++ API reference "
            "decision.")


def results(jobs):
    return {job: os.environ.get(f"{job.upper()}_RESULT", "") for job in jobs}


def main():
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        results(CXX_JOBS),
        os.environ.get("TOKENS_NEEDED", ""),
        results(TOKEN_JOBS)) or reference_error(
            os.environ.get("CPP_REFERENCE_NEEDED", ""),
            os.environ.get("CPP_REFERENCE_RESULT", "")) or site_error(
                os.environ.get("SITE_NEEDED", ""), results(SITE_JOBS))
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation, C++, design-token, C++ API reference and "
        "documentation site job results match the fail-closed change "
        "classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
