# Request Context Conformance

Source fixture: `examples/request-context/app.js`.

Build command:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/request-context/app.js --out <artifacts>
```

Artifact expectations:

- deterministic `app.plan.json`, `app.js`, and `app.js.map`;
- one GET route for `/users/{id:int}`;
- one registered handler ID.

Run command when V8 is enabled:

```powershell
sloppy run --artifacts <artifacts> --once GET /users/123?q=abc&q=last
```

Expected output: a JSON response containing route ID `123`, query value `last`,
`request.method` as `GET`, `request.path` as `/users/123`, and
`request.rawTarget` as `/users/123?q=abc&q=last`.

Gated requirements: execution requires V8. Header and body context are implemented in the
HTTP runtime and covered by HTTP parser/dispatch tests plus V8-gated HTTP integration.
This conformance fixture remains focused on route/query/request-target behavior until the
socket-mode conformance layer can send deterministic body-bearing requests.
