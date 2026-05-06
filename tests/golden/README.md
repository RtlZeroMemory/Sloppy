# Golden Tests

Golden tests will cover:

- emitted `app.js`;
- emitted `app.js.map`;
- emitted `app.plan.json`;
- diagnostics text and machine-readable codes.

Current checked-in plan fixtures under `tests/golden/plan/` define the intended minimal
Plan v1 parser inputs. `tests/golden/plan/README.md` lists every fixture, expected outcome,
diagnostic code, and parser coverage status.
CLI fixtures under `tests/golden/cli/` pin routes, capabilities, doctor, audit, and
OpenAPI output. Network, HTTP client, and OS doctor/audit goldens are metadata evidence
only: they must not include live external network claims, raw URLs that carry secrets,
cookies, authorization headers, bearer tokens, API keys, TLS-sensitive material,
environment values, secret process args, captured process output, raw PIDs, native
handles, public alpha claims, or benchmark claims.

Golden files should be reviewed like public API changes.
