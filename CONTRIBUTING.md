# Contributing

This guide is for human contributors working in the Sloppy repository.

Automation-specific operating rules are documented in
[AGENTS.md](AGENTS.md) and [AGENTS_CONTRIBUTING.md](AGENTS_CONTRIBUTING.md).

## Start Here

- [Documentation home](docs/README.md)
- [Building from source](docs/contributor/building-from-source.md)
- [Development scripts](docs/contributor/dev-scripts.md)
- [Testing](docs/contributor/testing.md)
- [Quality gates](docs/contributor/quality-gates.md)
- [Coding standards](docs/contributor/coding-standards.md)
- [Documentation policy](docs/contributor/documentation.md)
- [Review playbook](docs/contributor/review-playbook.md)

## Local Setup (Windows x64)

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

When a change affects runtime execution or other V8-adjacent behavior, also run:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

If a required tool or SDK is missing, report that failure directly. Do not
present blocked lanes as successful lanes.

## Working Rules

1. Keep each PR scoped to one coherent behavior slice.
2. Update code, tests, and docs together when behavior changes.
3. Prefer removing replaced pre-alpha shapes over adding compatibility layers.
4. Run the relevant checks before opening a PR.
5. Review staged files for generated artifacts or scope creep before push.

## Evidence Reporting

Use only these lane statuses in PR evidence:

- `PASS`
- `FAIL`
- `SKIPPED`
- `UNAVAILABLE`
- `DEFERRED`
- `NOT RUN`

Skipped optional lanes are not pass evidence. Separate non-V8, V8, package,
live-provider, stress, and benchmark lanes when reporting.

## Documentation Expectations

- Write for readers, not for planning ceremony.
- Keep docs aligned with current implementation evidence.
- Keep Diataxis page intent clear (tutorial/how-to/reference/explanation).
- Avoid stale planning language and unsupported claims.

## PR Checklist

- Clear contract and non-goals.
- Tests for expected and negative paths when applicable.
- Honest lane report with run/skip reasons.
- Goldens updated only when intended and explained.
- Secret/redaction checks when the change touches diagnostics or outputs.
