import os
import sys

FOUNDATION_JOBS = ("changes", "actionlint", "settings", "conformance")
CXX_JOBS = ("build", "sanitizers", "format", "tidy")


def validation_error(foundation_results, cxx_needed, cxx_results):
    results = foundation_results.split()
    if (len(results) != len(FOUNDATION_JOBS)
            or any(result != "success" for result in results)):
        return ("Every foundation CI job must succeed: "
                + ", ".join(FOUNDATION_JOBS) + ".")
    if cxx_needed == "true":
        if any(cxx_results.get(job) != "success" for job in CXX_JOBS):
            return ("C++ changes require successful build, sanitizer, "
                    "formatting and lint jobs.")
        return ""
    if cxx_needed == "false":
        if any(cxx_results.get(job) != "skipped" for job in CXX_JOBS):
            return (
                "Only classifier-approved documentation changes may skip "
                "the C++ jobs.")
        return ""
    return "The change classifier did not produce a valid C++ decision."


def main():
    cxx_results = {
        job: os.environ.get(f"{job.upper()}_RESULT", "") for job in CXX_JOBS}
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        cxx_results)
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation and C++ job results match the fail-closed "
        "change classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
