# Auth Runtime

Alpha / Experimental: provider middleware, `ctx.user`, and route `auth` metadata
are alpha runtime surfaces and may change between revisions.

The app host runs auth as middleware plus route authorization metadata.
Provider middleware authenticates credentials and assigns `ctx.user`. Route
authorization then evaluates the matched route's `auth` metadata before the
handler executes.

Fail-closed rules:

- malformed bearer headers return `401`;
- invalid JWT signatures return `401`;
- missing API keys on protected routes return `401`;
- authenticated users missing scopes, roles, claims, or policies return `403`;
- unknown route policy names return `403`;
- CSRF failures return `403`;
- `allowAnonymous()` routes bypass route authorization.

Cookie sessions have two runtime modes. Signed-cookie mode keeps claims and
expiry in the authenticated cookie and defaults to a 24-hour `Max-Age` when the
app does not set `maxAgeSeconds`. Store-backed mode signs only an opaque
session ID and keeps claims, idle expiry, absolute expiry, revocation time,
CSRF token, and metadata in the configured session store. Memory stores are
available for dev/test and Plan-visible diagnostics; data-provider stores use
the `sloppy_auth_sessions` table through an opened Sloppy data connection.

`sameSite: "none"` requires secure cookies. The default CSRF cookie uses a
`__Host-` prefix, so Sloppy rejects CSRF configurations that combine that name
with `secure: false` or a path other than `/`.

Compiler source input emits auth schemes, route requirements, scopes, policies,
and anonymous route metadata into the Plan. Secret values are represented only
by config references or `<redacted>` markers.
