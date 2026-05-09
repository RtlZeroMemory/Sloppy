# `sloppy audit`

Run security and policy checks against a Plan. Read-only; doesn't enter V8.

```
sloppy audit --plan <path> [--format text|json]
```

`<path>` is an `app.plan.json` file or a directory containing one.

## What it checks

- Capability surface: every declared capability has a known provider.
- Secret handling: no credential strings appear in Plan metadata.
- Process and network usage flags: anything declared at compile time that
  would expand the trust surface.
- Provider connection strings: configured via env / config keys, not baked
  into source.

The exact list of checks is part of the audit output, so you don't have to
remember it.

## Output

**Text** (default):

```
$ sloppy audit --plan .sloppy/app.plan.json
plan-secrets-redacted   : pass
provider-config-source  : pass
process-spawn           : not declared
network-egress          : not declared
```

**JSON** is the same data, suitable for piping into compliance tooling.

## Exit codes

- `0` when every check passes.
- `1` when one or more checks fail.

## When to use

- Before publishing artifacts.
- In CI as a hard gate.
- After upgrading the Sloppy runtime, to surface new policy checks the
  newer compiler emits metadata for.

For provider availability and environment health, use
[`sloppy doctor`](doctor.md) instead.
