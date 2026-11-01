import os
import sys


def validation_error(
        foundation_results, cxx_needed, build_result, sanitizer_result):
    results = foundation_results.split()
    if not results or any(result != "success" for result in results):
        return "Every foundation CI job must succeed."
    if cxx_needed == "true":
        if build_result != "success" or sanitizer_result != "success":
            return "C++ changes require successful build and sanitizer jobs."
        return ""
    if cxx_needed == "false":
        if build_result != "skipped" or sanitizer_result != "skipped":
            return (
                "Only classifier-approved documentation changes may skip "
                "C++ and sanitizer jobs.")
        return ""
    return "The change classifier did not produce a valid C++ decision."


def main():
    error = validation_error(
        os.environ.get("FOUNDATION_RESULTS", ""),
        os.environ.get("CXX_NEEDED", ""),
        os.environ.get("BUILD_RESULT", ""),
        os.environ.get("SANITIZERS_RESULT", ""))
    if error:
        print(error, file=sys.stderr)
        return 1
    print(
        "Required foundation, build, and sanitizer results match the "
        "fail-closed change classification.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
