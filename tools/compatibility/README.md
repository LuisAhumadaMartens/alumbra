# Compatibility matrix data

`matrix.json` is the source for the compatibility tables in `docs/compatibility.md` and the release-test candidate
input in `release-testing.json`. `schema.json` describes the data shape; `generate.py` validates the stricter
project rules and emits both files.

Each platform row and each compatibility table has an evidence reference. A table's evidence reference applies to
every non-empty cell marked `●` or `◐` in that table. The validator rejects missing or unknown references and
requires secure HTTPS URLs. A dash or `n/a` cell does not make a support claim and does not need evidence.

Tier definitions are fixed to [ADR-0039](../../docs/adr/0039-compatibility-matrix-and-support-tiers.md). A matrix
change cannot redefine their release cadence. Tier 1 uses CI on each change, or a named device on a fixed cadence
where CI cannot run; tier 2 uses a reference device before each release; tier 3 is best effort and does not block a
release.

Regenerate the committed outputs after changing the data:

```sh
python3 tools/compatibility/generate.py
```

Check the data, evidence references and committed outputs with:

```sh
python3 tools/compatibility/generate.py --check
python3 -m unittest discover -s tools/compatibility -p 'test_*.py'
```

`release-testing.json` includes tier 1 and tier 2 platform floors and one candidate per supported capability cell,
with the evidence URL and label carried into each record for downstream release tooling.
