# Diagnostics

Status: Bootstrap JavaScript errors exist for current public API validation; native
diagnostic rendering remains planned.

Purpose: document current user-facing error behavior and the future path to stable native
diagnostics, source spans, hints, and redaction behavior.

Implemented bootstrap module diagnostics are JavaScript `Error` or `TypeError` values.
They are intentionally plain, but messages include useful context:

- invalid module object;
- invalid module name;
- duplicate module name;
- mutation after a module is added to a builder;
- missing module dependency;
- module dependency cycle;
- module phase callback failure.

Example missing dependency message:

```text
sloppy: module dependency missing

Module:
  users

Missing dependency:
  data

Fix:
  builder.addModule(/* module named 'data' */) before build()
```

Example phase failure message:

```text
sloppy: module phase failed

Module:
  users

Phase:
  routes

Reason:
  route boom
```

Existing bootstrap errors also cover invalid config keys, invalid log levels, duplicate or
missing service tokens, invalid route patterns, invalid route groups, invalid result
options, invalid schemas, and mutation after builder/app freeze.

Not implemented yet: stable native diagnostic codes for module errors, source spans,
source maps, rendered code frames, JSON diagnostic output, redaction enforcement, runtime
startup diagnostics for module graphs, and compiler extraction diagnostics.

## CLI Diagnostics

The initial `sloppy doctor` and `sloppy audit` commands produce deterministic text output
and machine-readable JSON output from metadata. They do not run app code or connect to live
providers by default.

`sloppy doctor` redacts connection-string-like secret fields such as `password`, `pwd`,
`token`, and `secret` before printing check messages. Provider checks that require live
servers or machine-local drivers remain opt-in future work.

`sloppy audit` emits fixed finding records with `severity`, stable `code`, `message`, and
`path` fields. The implemented rules are intentionally small and metadata-only.

Related internal docs: `docs/diagnostics.md`, `docs/modularity.md`.
