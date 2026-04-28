# Golden Tests

Golden tests will cover:

- emitted `app.js`;
- emitted `app.js.map`;
- emitted `app.plan.json`;
- diagnostics text and machine-readable codes.

Current checked-in plan fixtures under `tests/golden/plan/` define the intended minimal
Plan v1 parser inputs before production JSON parsing exists.

Golden files should be reviewed like public API changes.
