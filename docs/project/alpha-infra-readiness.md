# ALPHA-INFRA Readiness Integration

Status: ALPHA-INFRA-01 readiness-gate input for #300.

`alpha-infra-readiness.json` is the machine-readable source for the ALPHA-INFRA slice of
the public alpha readiness gate. It records what the infrastructure waves completed, which
evidence lanes exist, and which lanes remain deferred, blocked, skipped, or unavailable.

This document is not final public alpha documentation and does not authorize a public
release. It is an internal readiness input that #300, #681, #685, #684, and #301 can
consume during consolidation and final gate review.

## Consumption Rules

- `complete` means the named ALPHA-INFRA issue has a merged implementation PR.
- `deferred` means the issue or evidence lane still has exact follow-up work.
- `blocked` means another named track owns the missing runtime/provider/compiler behavior.
- `skipped`, `unavailable`, and `not-run` are never pass evidence.
- Package evidence is separate from release readiness.
- V8 evidence is separate from default non-V8 evidence.
- Live-provider evidence is separate from default provider diagnostics.

## Current Integration Shape

- Bootstrap, dependency, V8 resolver, dev-script, packaging, smoke, release dry-run, and
  no-claims guardrails are represented.
- Windows x64 V8 SDK provisioning has a pinned, checksum-validated GitHub release asset
  consumed by `tools/windows/fetch-v8.ps1`; Linux and macOS SDK artifacts remain planned.
- The dogfood catalog names `hello` artifact/source-input/package lanes and future feature
  apps without claiming unfinished HTTP, TLS, provider, or Framework behavior.

The final public documentation pass and final alpha verification remain outside this
ALPHA-INFRA loop.
