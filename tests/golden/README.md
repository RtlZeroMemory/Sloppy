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
OpenAPI output. Network, HTTP client, and OS doctor/audit goldens are metadata coverage
only: they must not include live external network statements, raw URLs that carry secrets,
cookies, authorization headers, bearer tokens, API keys, TLS-sensitive material,
environment values, secret process args, captured process output, raw native handles,
pid-as-handle capabilities, release publishing statements, or benchmark comparisons.

Golden files should be reviewed like public API changes. Treat each golden as
a semantic contract for the user-visible behavior it pins.

## Golden Policy

Goldens are receipts, not the source of truth. Structured JSON goldens should
assert stable semantic fields; text goldens are reserved for deliberate UX
surfaces. Do not update a golden only because current output changed.

Compiler artifact goldens must pass the semantic compiler-contract validator
before snapshot comparison. A validator failure means the compiler or fixture
expectation is wrong; do not bless the failure by updating `app.plan.json`,
`routes.slrt`, dependency graph, or alpha/template goldens.

Goldens must normalize:

- repository, workspace, temp, and user paths;
- source locations when the exact local path is not the behavior under test;
- line endings;
- timestamps, process IDs, nondeterministic IDs, ports, and platform-specific values;
- secret-looking values, tokens, connection strings, passwords, private keys, and
  passphrases.

Golden updates must explain the intended behavior change in the PR. Redaction checks must
assert that real or marker secrets such as `SECRET_SHOULD_NOT_APPEAR` do not appear in
diagnostic, Plan, doctor, or CLI goldens.
