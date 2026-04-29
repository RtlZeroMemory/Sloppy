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

Related internal docs: `docs/diagnostics.md`, `docs/modularity.md`.
