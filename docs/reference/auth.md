# Auth Reference

Auth diagnostics use stable `SLOPPY_E_AUTH_*` and `SLOPPY_E_SECURITY_*` code
prefixes in ProblemDetails responses and tooling output.

Common runtime codes:

- `SLOPPY_E_AUTH_UNAUTHORIZED` for missing or invalid credentials.
- `SLOPPY_E_AUTH_FORBIDDEN` for authenticated users that fail policy, role,
  scope, or claim checks.
- `SLOPPY_E_AUTH_CSRF_FAILED` for missing or invalid CSRF tokens.

Route metadata fields:

- `auth.required`
- `auth.allowAnonymous`
- `auth.schemes`
- `auth.scopes`
- `auth.roles`
- `auth.claims`
- `auth.policy`

`auth.schemes` stores configured scheme names. When a route is protected but
does not list schemes, runtime checks use the app default scheme if one was
configured. Anonymous route metadata overrides inherited group auth metadata.

Plan auth scheme metadata redacts secret-bearing values. Config key names may
appear so `sloppy run`, `sloppy doctor`, `sloppy audit`, and `sloppy openapi`
can validate the app without leaking secret values.

Cookie-session scheme metadata may include `store`, `idleTimeoutMs`,
`absoluteTimeoutMs`, and `rotation`. Tooling reports in-memory stores so release
reviews can distinguish dev/test sessions from durable data-provider sessions.
