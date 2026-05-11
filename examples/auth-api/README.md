# Auth API

Small source-input example for Sloppy JWT bearer, API key, roles, policies, and
Plan/OpenAPI auth metadata.

Build from this directory:

```sh
sloppy build
sloppy routes .sloppy
sloppy openapi .sloppy --output openapi.json
```

`appsettings.json` uses placeholder secrets. Replace them locally or inject
real values through your deployment configuration. Do not commit real auth
secrets.

Runtime handler execution requires a V8-enabled Sloppy build. `sloppy run
--once` is useful for public routes, but it does not provide a convenient
manual header input path for protected-route testing.
