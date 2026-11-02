# Security policy

Alumbra parses untrusted input (manifests, containers, subtitles, ad responses) from the network, which makes
memory-safety and parsing bugs security-relevant.

## Reporting a vulnerability

Please **do not open a public issue**. Report privately through
[GitHub Security Advisories](https://github.com/LuisAhumadaMartens/alumbra/security/advisories/new). Include
affected versions, a description, and a minimal reproducer (e.g. a crafted file or manifest).

You will receive an acknowledgement, and fixes will be coordinated with you before disclosure.

## Scope

In scope: crashes, memory corruption, out-of-bounds reads/writes, infinite loops or unbounded memory growth
triggered by media, manifests, subtitles or ad responses; leaks of DRM or ad credentials.

Out of scope: vulnerabilities in third-party platform SDKs (report them upstream), and DRM circumvention.

## Hardening practices

Parsers are fuzzed with libFuzzer, CI runs ASan/UBSan/TSan, and network input is size-bounded by settings.
CodeQL scans the C++ code and the GitHub Actions workflows with the `security-extended` queries on every pull
request, on every push to `main` and weekly. Dependabot proposes updates to the vcpkg baseline and to the
commit-pinned actions; each goes through the full CI and licence review before merging. See
[code scanning and dependency updates](docs/process.md#code-scanning).
