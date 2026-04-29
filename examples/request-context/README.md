# Request Context Example

Status: compiler-input example for the dev-only V8 `sloppy run --artifacts` path.

This example demonstrates the EPIC-23 request context shape:

- `route.id` comes from `/users/{id:int}`;
- `query.q` comes from the query string;
- `request.path` comes from the parsed request target.

Build artifacts:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/request-context/app.js --out .sloppy-test
```

Run one deterministic request with a V8-enabled build:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy-test --once GET /users/123?q=abc
```

This is still dev-only. Route `{id:int}` validates the matched path segment, but the handler
receives `route.id` as the string `"123"`. Query values are strings, repeated query keys use
last-wins behavior, and `%XX`/`+` query decoding is supported. Request body parsing,
headers in context, middleware, streaming, cookies, content negotiation, package-manager
behavior, and Node compatibility are not implemented.
