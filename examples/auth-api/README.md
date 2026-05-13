# Auth API

Small source-input example for Sloppy JWT bearer, rotating memory-backed session
cookies, API keys, roles, policies, and Plan/OpenAPI auth metadata.

Build from this directory:

```sh
sloppy build
sloppy routes .sloppy
sloppy openapi .sloppy --output openapi.json
```

`appsettings.json` declares sample JWT, session, and API-key secret keys for
local builds. Provide deployment values through environment-specific
configuration. Do not commit real auth secrets.

Runtime handler execution requires a handler-capable Sloppy build. `sloppy run
--once` is useful for public routes, but it does not provide a convenient
manual header input path for protected-route testing.
