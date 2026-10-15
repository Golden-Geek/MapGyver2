# Diagnostic CLI v1

`mapgyver-engine --version` prints product/version and exits 0.
`--diagnose [--json] [--no-gpu]` reports compiled platform, configuration,
compiler/CMake and source identity. `--help` (or no arguments) prints usage.
Repeated/unknown flags and incompatible command combinations exit 2 with usage
on stderr and no success payload. Mandatory diagnostic/output failures exit 1.

The JSON contract is [diagnostics v1](../schemas/diagnostics-v1.schema.json).
JSON goes exclusively to stdout. GPU probing is never initialized with
`--no-gpu`: status is `not_probed` / `disabled_by_request`. M00 without that flag
reports `unavailable` / `gpu_probe_not_implemented`; it cannot claim GPU success.
Every configured feature remains represented with a capability reason.

Git revision and dirty state describe sources at build time, refreshed for each
build. Both are null in a source archive without its own Git repository.
Unknown provenance is not a clean checkout. A schema version change is required
for incompatible JSON changes. Python standard-library contract tests validate
the schema subset actually used and reject unknown schema keywords.
