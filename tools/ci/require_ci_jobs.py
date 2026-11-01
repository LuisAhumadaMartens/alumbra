import os
import sys

CXX_JOBS = ("build", "sanitizers", "format", "tidy")


def validation_error(foundation_results, cxx_needed, cxx_results):
    results = foundation_results.split()
    if not results or any(result != "success" for result in results):
        return "Every foundation CI job must succeed."
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


def main():
    cxx_results = {
        job: os.environ.get(f"{job.upper()}_RESULT", "") for job in CXX_JOBS}
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        cxx_results) or reference_error(
            os.environ.get("CPP_REFERENCE_NEEDED", ""),
            os.environ.get("CPP_REFERENCE_RESULT", ""))
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation, C++ and C++ API reference job results match "
        "the fail-closed change classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
