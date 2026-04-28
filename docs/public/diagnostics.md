# Diagnostics

Status: Planned / not implemented yet.

Purpose: document future user-facing diagnostics, stable codes, source spans, hints, and
redaction behavior.

Planned diagnostic shape:

```text
error[SLP_SERVICE_MISSING]: service not registered

  Route:
    POST /users

  Missing service:
    data.main
```

This output is aspirational and not currently implemented.

Related internal docs: `docs/diagnostics.md`.
