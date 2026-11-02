import os
import sys

FOUNDATION_JOBS = ("changes", "actionlint", "settings", "licence", "conformance")
CXX_JOBS = ("build", "sanitizers", "fuzz", "format", "tidy")
TOKEN_JOBS = ("tokens", "tokens_swift")


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
                            "Design-token", "CSS, TypeScript, Kotlin and Swift",
                            "changes without design-token inputs"))


def results(jobs):
    return {job: os.environ.get(f"{job.upper()}_RESULT", "") for job in jobs}


def main():
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        results(CXX_JOBS),
        os.environ.get("TOKENS_NEEDED", ""),
        results(TOKEN_JOBS))
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation, C++ and design-token job results match the "
        "fail-closed change classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
