# `sloppy audit`

Run static policy checks against a compiled Plan. `audit` is read-only and does
not enter V8.

```sh
sloppy audit --plan <path> [--format text|json]
sloppy audit --artifacts <dir> [--format text|json]
```

`<path>` is an `app.plan.json` file or a directory containing one.
`--artifacts <dir>` is equivalent to `--plan <dir>/app.plan.json`.

## What it checks

- route capability references;
- duplicate route names and route method/pattern pairs;
- handler references;
- module dependency references;
- provider metadata shape;
- provider/capability consistency;
- filesystem and network capability visibility.

## Text output

Current text output lists each finding with severity, code, message, and area:

```text
Sloppy Audit

[error] SLOPPY_AUDIT_ROUTE_CAPABILITY_MISSING route references an undeclared capability (routes)
[error] SLOPPY_AUDIT_DUPLICATE_ROUTE_NAME duplicate route name (routes)
[error] SLOPPY_AUDIT_DUPLICATE_ROUTE duplicate route method and pattern (routes)
[error] SLOPPY_AUDIT_MISSING_HANDLER route references a missing handler id (routes)
[error] SLOPPY_AUDIT_MISSING_MODULE_DEPENDENCY module dependency is missing (modules)
[warn] SLOPPY_AUDIT_PROVIDER_INCOMPLETE provider metadata is missing token, provider, or service (dataProviders)
[error] SLOPPY_AUDIT_PROVIDER_MISMATCH capability provider reference does not match data provider (capabilities)
[error] SLOPPY_AUDIT_CAPABILITY_INSUFFICIENT capability access is insufficient for provider operation (capabilities)
[error] SLOPPY_AUDIT_DUPLICATE_CAPABILITY duplicate capability token (capabilities)
[error] SLOPPY_AUDIT_CAPABILITY_PROVIDER_MISSING database capability references an undeclared provider (capabilities)
[error] SLOPPY_AUDIT_CAPABILITY_PROVIDER_REQUIRED database capability is missing required provider reference (capabilities)
[error] SLOPPY_AUDIT_CAPABILITY_PROVIDER_FORBIDDEN filesystem/network capabilities must not declare providers (capabilities)
[note] SLOPPY_AUDIT_FILESYSTEM_POLICY_VISIBLE filesystem capabilities are policy-visible for sloppy/fs; no OS sandbox is implemented (capabilities)
[note] SLOPPY_AUDIT_NETWORK_POLICY_VISIBLE network capabilities are policy-visible for sloppy/net, including LocalEndpoint metadata; no OS sandbox or external live-network evidence is implemented (capabilities)
```

Clean Plans produce fewer rows. JSON output carries the same findings in a
machine-readable form.

## Exit codes

| Code | Meaning |
| --- | --- |
| `0` | No errors |
| `1` | At least one `error` finding |

Warnings and notes describe visible policy surface but do not fail the command.
