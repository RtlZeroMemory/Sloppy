# Getting Started

Status: Bootstrap app-host, developer ergonomics, module skeleton, compiler extraction MVP,
and dev-only artifact run MVP implemented.

Purpose: introduce the future Sloppy developer workflow from app source to build artifacts
to runtime execution.

Current bootstrap API shape:

```ts
import { Sloppy, Results, schema } from "sloppy";

const builder = Sloppy.createBuilder();

builder.services.addSingleton("message", () => "Sloppy is alive");
builder.addModule(Sloppy.module("hello")
  .services(services => {
    services.addSingleton("hello.module", () => "Hello from a module");
  }));

const app = builder.build();

const Query = schema.object({
  q: schema.string().min(1),
});

app.mapGroup("/search")
  .withTags("Search")
  .mapGet("/", { query: Query }, ({ services }) => Results.ok({
    message: services.get("message"),
  }));
```

`Sloppy.createBuilder()`, `builder.build()`, `Sloppy.create()`, `app.mapGet(...)`,
`app.mapGroup(...)`, `app.freeze()`, `Sloppy.module(...)`, `builder.addModule(...)`, the
current `Results.*` helper set, object-backed config, memory logging, string-token
singleton/transient services, and the `schema` skeleton now exist in the bootstrap stdlib.
`app.run()` and `app.listen()` do not. The broader example remains aspirational as a
runnable application until the ENGINE-01 foundation contract is implemented across the
compiler, V8 async runtime, HTTP runtime, and SQLite bridge.

The first checked-in example lives at `examples/hello/`. It uses the current source stdlib
path as a bootstrap API-shape fixture, not as the compiler input contract:

```js
import { Sloppy, Results } from "../../stdlib/sloppy/index.js";
```

`examples/hello/app.js`, `examples/ergonomics/app.js`, and `examples/modules-basic/app.js`
are bootstrap API-shape examples and are statically checked by CTest. They are not the
compiler input contract and do not run through a real HTTP server.

The compiler MVP example lives at `examples/compiler-hello/` and uses the supported bare
`"sloppy"` input shape:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy-main-smoke
```

The same supported MAIN hello path can be exercised with an installed `sloppyc` binary
when it is available:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

The build command emits `.sloppy-main-smoke/app.plan.json`,
`.sloppy-main-smoke/app.js`, and `.sloppy-main-smoke/app.js.map`. With a V8-enabled
build, those artifacts can be served by the dev-only run MVP:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy-main-smoke --host 127.0.0.1 --port 5173
```

For deterministic smoke tests without opening a socket:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy-main-smoke --once GET /
```

`sloppy run` requires V8, loads prebuilt artifacts only in this MVP, and is not a
production server. EPIC-23 adds route/query/request context and supported
`Results.text/json/ok/noContent/problem` response descriptors. There is no HTTPS, request
body parsing, headers in handler context, streaming, middleware, hot reload, package
manager behavior, Node compatibility, or production response pipeline.

ENGINE-01 keeps public alpha docs blocked until the foundation contract in
`docs/project/engine-framework-contract.md` has executable evidence for realistic examples:
async handlers, cancellation, JSON/text bodies, headers, SQLite, capability denial, and
packaged runtime smoke. Direct source input is tracked by #302; until that lands, examples
should continue to show explicit `sloppyc build` plus `sloppy run --artifacts`.

The request-context compiler example lives at `examples/request-context/`.
MAIN1-13 adds executable conformance around the public compiler examples: default tests
compile the hello and request-context sources and verify deterministic artifacts, while
V8-gated tests run those compiled artifacts through `sloppy run --artifacts --once`.
SQLite has a real V8-gated bridge fixture, but the source-stdlib `examples/sqlite-basic/`
example remains marked as API-shape/static until the compiler can emit that app shape.

Related internal docs: `docs/architecture.md`, `docs/execution-model.md`,
`docs/developer-ergonomics.md`.
