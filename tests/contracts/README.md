# Contract Tests

Contract tests verify that generated or packaged artifacts are semantically
honest. Goldens pin bytes that should not drift; contract validators inspect an
artifact set and fail when the bytes describe something unusable or misleading.

Run the PR-tier contract lane with:

```powershell
node tests/contracts/runner/contract-runner.mjs --area package --tier pr
node tests/contracts/runner/contract-runner.mjs --area release --tier pr
node tests/contracts/runner/contract-runner.mjs --area all --tier pr
node tests/contracts/runner/contract-runner.mjs --area package --tier pr --format markdown
node tests/contracts/runner/contract-runner.mjs --area package --tier pr --out artifacts/contracts/package-report.json
```

The test engine exposes the same lane:

```powershell
tools/windows/test-engine.ps1 -Area contracts -Tier pr
```

```sh
tools/unix/test-engine.sh --area contracts --tier pr
```

## Report Format

Validators emit JSON reports with:

- `schemaVersion`, `subsystem`, `tier`, `startedAt`, and `finishedAt`;
- `findings[]` entries with `id`, `status`, `severity`, `subsystem`,
  `invariant`, optional `fixture`, optional `path`, `message`, and `details`;
- `summary` counts for `pass`, `fail`, `warning`, `skip`, and `unavailable`.

Statuses are lower-case: `pass`, `fail`, `skip`, and `unavailable`. A fail or
error finding makes the runner exit non-zero. Skipped and unavailable findings
stay visible and are not counted as passes.

## Adding a Contract

Add a focused validator under `tests/contracts/<area>/`, then register it in
`tests/contracts/runner/contract-runner.mjs`. Keep the interface simple: the
validator should return a report for one subsystem and should use the shared
runner helpers for reporting, fixture loading, path checks, and redaction.

PR-tier contracts should be deterministic and should not require Docker, Redis,
live providers, npm publishing, or long-running servers. Use `extended` or
`torture` for slow or environment-backed checks.
