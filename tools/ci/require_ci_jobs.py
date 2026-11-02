import os
import sys

CXX_JOBS = ("build", "sanitizers", "format", "tidy")
SITE_JOBS = ("prose", "site")


def classified_error(needed, results, jobs, kind, summary):
    if needed == "true":
        if any(results.get(job) != "success" for job in jobs):
            return f"{kind} changes require successful {summary} jobs."
        return ""
    if needed == "false":
        if any(results.get(job) != "skipped" for job in jobs):
            return (f"Only changes the classifier keeps away from the {kind} "
                    f"checks may skip the {summary} jobs.")
        return ""
    return f"The change classifier did not produce a valid {kind} decision."


def validation_error(foundation_results, cxx_needed, cxx_results,
                     site_needed, site_results):
    results = foundation_results.split()
    if not results or any(result != "success" for result in results):
        return "Every foundation CI job must succeed."
    return (classified_error(cxx_needed, cxx_results, CXX_JOBS, "C++",
                             "build, sanitizer, formatting and lint")
            or classified_error(site_needed, site_results, SITE_JOBS,
                                "documentation site",
                                "prose and site build"))


def job_results(jobs):
    return {job: os.environ.get(f"{job.upper()}_RESULT", "") for job in jobs}


def main():
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        job_results(CXX_JOBS),
        os.environ.get("SITE_NEEDED", ""),
        job_results(SITE_JOBS))
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation, C++ and documentation site job results match "
        "the fail-closed change classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
