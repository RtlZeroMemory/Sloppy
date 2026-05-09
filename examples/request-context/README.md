# Request Context Example

This is a compiler-input example for the dev-only V8 `sloppy run --artifacts` path.
This example demonstrates the current request context shape:

- `route.id` comes from `/users/{id:int}`;
- `query.q` comes from the query string;
- `request.method` is the parsed HTTP method;
- `request.path` comes from the parsed request target.
- `request.rawTarget` preserves the original target string.

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
last-wins behavior, and `%XX`/`+` query decoding is supported.

Headers and JSON/text/byte body helpers exist in the broader request-context runtime and
app test host lanes. This small example intentionally stays on fields that `sloppy run
--once` can exercise from only a method and target. Middleware, streaming request APIs,
cookies, and content negotiation are not covered by this request-context
example.
