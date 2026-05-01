# Framework API Shape

Status: design lock for FRAMEWORK-01 before implementation. This document is source of
truth for the intended post-Core framework/API ergonomics; it does not claim the current
stdlib/compiler/runtime already implement every shape below.

## Current vs Target

| Area | Current proven path | Locked target |
| --- | --- | --- |
| Run workflow | `sloppy run app.js`, `sloppy run` through `sloppy.json`, and explicit `sloppy run --artifacts` | Later TypeScript lowering keeps the same compiler/artifact/runtime path. |
| Route API | Current compiler recognizes `app.mapGet/mapPost/...` | Minimal API `app.get/post/put/patch/delete(...)` first. |
| Modules | Bootstrap module skeleton and single-file compiler subset | Function modules plus relative imports for real apps. |
| Providers | `data.sqlite("main")` bridge proof | Explicit provider import: `import { sqlite } from "sloppy/providers/sqlite"`. |
| Capabilities | Plan metadata and runtime checks exist | Compiler/Plan-generated capabilities by default. |

## Minimal API First

```ts
import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
const db = app.use(sqlite("main"));

app.get("/health", () => Results.text("ok"));

app.get("/users/:id", async (ctx) => {
  const id = ctx.route.int("id");
  const user = await db.queryOne("select id, name, email from users where id = ?", [id]);
  return user ? Results.json(user) : Results.notFound();
});

export default app;
```

Locked decisions:

- Minimal API is the primary framework style.
- Route registration is fluent and startup-only.
- Handler shape is `(ctx) => ResultLike | Promise<ResultLike>`.
- Express-style `req, res, next` is not the target shape.
- The style is .NET-inspired, not .NET-copied.

## Function Modules First

```ts
// app.ts
const app = Sloppy.create();

app.use(sqlite("main"));
app.useModule(usersModule);

export default app;

// users.ts
export function usersModule(app: SloppyApp) {
  const db = app.provider("sqlite:main");
  const api = app.group("/api");
  const users = api.group("/users");

  users.get("/", async () => Results.json(await db.query("select * from users")));
}
```

Locked decisions:

- Function modules are the first modularization mechanism.
- Relative multi-file imports, Slop stdlib imports, provider imports, and function modules
  are required for framework MVP.
- Module registration compiles into the same Plan route/provider/capability graph.
- COMPILER-30.D supports direct module-app routes and nested literal module route groups;
  controllers, decorators, filters, and middleware are still later shapes.
- npm resolution, `package.json` semantics, TS path aliases, dynamic import, and broad
  module systems remain deferred.

## Controllers Later

```ts
class UsersController {
  list(ctx) { /* ... */ }
  create(ctx) { /* ... */ }
}

app.controller("/users", UsersController, c => {
  c.get("/", "list");
  c.post("/", "create");
});
```

Controllers are a target shape, not a framework MVP dependency. Controllers must compile or
desugar into the same route graph as Minimal API. Decorators, controller implementation,
constructor injection, and full DI are deferred.

## Provider and Capability Inference

Normal developers should not hand-write capability declarations for ordinary apps.

```ts
const db = app.use(sqlite("main"));

app.get("/users", async () => db.query("select * from users"));
app.post("/users", async () => db.exec("insert into users ..."));
```

Capability layers:

1. Inferred capabilities from recognized provider calls.
2. Provider config/access policy, such as `app.use(sqlite("main", { access: "readwrite" }))`.
3. Explicit advanced policy for locked-down or unusual cases.

Inference rules:

- `db.query` and `db.queryOne` imply read.
- `db.exec` and insert/update/delete-style operations imply write.
- Transactions and mixed read/write paths imply readwrite.
- If the compiler cannot infer safely, it must fail with a helpful diagnostic or require
  explicit metadata.
- Every inferred capability must be inspectable through future Plan, doctor, routes, and
  capabilities surfaces.
- Capability metadata should include source location when available.

## Configuration

Configuration is convention-first, typed, layered, and Plan-visible.

Default precedence:

1. defaults;
2. `appsettings.json`;
3. `appsettings.{Environment}.json`;
4. environment variables;
5. CLI args;
6. explicit runtime/test overrides.

```json
{
  "Sloppy": {
    "Server": {
      "Host": "127.0.0.1",
      "Port": 5173
    },
    "Providers": {
      "sqlite": {
        "main": {
          "database": "./app.db"
        }
      }
    }
  }
}
```

```ts
const port = app.config.getInt("Sloppy:Server:Port", 5173);
const options = app.config.bind("Sloppy:Providers:sqlite:main", SqliteOptions);
const providerOptions = app.config.bind("sqlite:main", SqliteOptions);
const db = app.use(sqlite("main"));
```

`bind` is implemented in this slice. It accepts colon keys and provider shorthand such as
`sqlite:main`, returns the matching subtree as an options object, and passes that object to
the supplied schema/constructor when one is provided.

Provider config is convention-bound by provider kind/name. Inline provider overrides are
allowed for tests and examples:

```ts
app.use(sqlite("main", { database: ":memory:" }));
```

Now: JSON files, environment selection, env var override, CLI override, typed get/bind,
provider config, and diagnostics. Later: reload-on-change, user secrets, custom config
providers, and remote config.

Slop config exposes Slop-owned keys such as `Sloppy:Server:Host`,
`Sloppy:Server:Port`, `Sloppy:Server:MaxConnections`,
`Sloppy:Server:MaxRequestBodyBytes`, `Sloppy:Server:RequestTimeoutMs`,
`Sloppy:Server:KeepAliveEnabled`, `Sloppy:Server:KeepAliveIdleTimeoutMs`,
`Sloppy:Server:MaxRequestsPerConnection`,
`Sloppy:Runtime:V8MicrotaskDrainLimit`, and
`Sloppy:Providers:sqlite:main:*`. It does not expose raw libuv handles, raw SQLite
pointers, or general V8 internals.

## Request Binding and Validation

Explicit `ctx` helpers come first:

```ts
const UserCreate = schema.object({
  name: schema.string().min(1),
  email: schema.string().email()
});

app.post("/users", async (ctx) => {
  const input = await ctx.body.json(UserCreate);
  return Results.created("/users", await createUser(input));
});
```

Later route-options binding:

```ts
app.post("/users", { body: UserCreate }, async (ctx) => {
  const input = ctx.body.value();
});
```

Locked decisions:

- Start with `ctx.route.int/string(...)`, `ctx.query.*`, `ctx.header.*`, and
  `ctx.body.json(schema)`.
- Schema metadata flows into Plan.
- Validation errors produce safe problem responses.
- Route-options binding may come after explicit helpers.
- Multipart/file upload and streaming request bodies are outside framework MVP.

Validation problem response target:

```json
{
  "type": "https://sloppy.dev/problems/validation",
  "title": "Validation failed",
  "status": 400,
  "errors": {
    "email": ["Invalid email address"]
  }
}
```

Validation errors must not include stack traces, raw internals, SQL parameters, secrets, or
unredacted config values. Stable diagnostic codes and shared redaction helpers are required.

## Results Model

Explicit `Results.*` helpers are primary:

- `Results.ok(value)`;
- `Results.json(value, status?)`;
- `Results.text(value, status?)`;
- `Results.created(location, value?)`;
- `Results.noContent()`;
- `Results.badRequest(problem?)`;
- `Results.notFound(problem?)`;
- `Results.problem(problem, status?)`.

Implicit object-to-JSON may be considered later, but is not primary now. Buffered,
non-streaming responses come first. Streaming/file responses are future HTTP/framework work.
Result descriptors must validate safely in native conversion. Response schemas may be
declared later through route options and Plan metadata.

HTTP keep-alive and chunked transfer framing are transport-owned, not framework result
APIs. HTTP-25.A/B/C may reuse one connection for sequential requests, and HTTP-25.D/E may
decode chunked request bodies into bounded full-body storage or write an internal/native
chunked response descriptor. Handlers still do not get a request streaming API, and no
public framework shape exposes pipelining, concurrent per-connection requests,
`Results.stream`, SSE/WebSockets, or file streaming.

## Services and DI

Do not build a full .NET DI container now.

Staged approach:

1. simple service registry, factories, and singleton-ish services;
2. class-token services;
3. controller constructor injection.

Provider handles are provider/capability objects, not just ordinary services, though they
may become injectable later.

## Plan-First DSL Extraction

Plan is the strategic center. The compiler recognizes the Slop app DSL; it does not try to
understand arbitrary JavaScript magic.

Plan graph should include app entry, modules, routes, methods, route patterns,
route/query/header/body params, body policies/schemas, providers, capabilities, config
keys, response shapes where declared, diagnostics/source maps, and later OpenAPI metadata.
COMPILER-30.H/I emits the first strong Plan layer for the implemented subset: source files,
function modules, route/plan completeness, provider-kind-aware effects, and compatibility
evidence. Provider facts use capability kind plus provider kind, so the framework can add
future provider families without treating SQLite or databases as the universal model.

Supported DSL recognition target:

- `Sloppy.create()`;
- `app.use(...)`;
- `app.useModule(...)`;
- `app.group(...)`;
- `app.get/post/put/patch/delete(...)`;
- `ctx.body.json(schema)`;
- `db.query/queryOne/exec/transaction`;
- `Results.*`;
- `schema.*`.

If route, provider, capability, body, config, or response behavior is not Plan-visible, it
is not part of the optimized/safe Slop framework path. Dynamic patterns should fail with
helpful diagnostics or require explicit metadata.

COMPILER-30 (#460) is the compiler-owned implementation roadmap for this DSL recognition
and inference. It should infer everything reasonable inside supported Slop app patterns,
including routes, groups, function modules, providers, config keys, request binding,
schemas, Results metadata, function effects, capabilities, source locations, diagnostics,
and Plan completeness. It must reject or require metadata for dynamic route paths, unknown
bare imports, npm/node_modules imports, dynamic imports, unknown runtime route generation,
and unresolvable provider usage where capability truth is required. This remains a
supported-subset compiler contract, not arbitrary TypeScript inference.

Manual route-level `uses` metadata and manual capability metadata are fallback escape
hatches only. Normal Minimal API, function-module, repository, factory, object-literal
method, and simple service/class patterns should get compiler-inferred effects and
capabilities when statically resolvable.

## Source Input and Multi-File Target

Source-input run is implemented for the current JavaScript compiler subset:

```powershell
sloppy run app.js
sloppy run
```

`sloppy.json` target:

```json
{
  "entry": "app.js",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

Internal flow: compile to `.sloppy/cache/dev/source-input` for positional source input or
to configured `outDir` for `sloppy.json`, validate the Plan/bundle/source map, then run
artifacts. This first source-input slice rebuilds every time; cache reuse is deferred until
the key includes source/import hashes, compiler/runtime/stdlib identity, target platform/
engine, environment, and feature/options. `sloppy build`, richer TypeScript input, watch
mode, package manager behavior, full TS typechecker, and `node_modules` resolution are
deferred.

Multi-file framework MVP inputs remain a target, not a claim from ENGINE-02.E. The current
compiler still supports the documented single-file source subset, bare `"sloppy"` stdlib
import, and SQLite provider metadata shape. Relative imports, explicit provider import
modules, and function modules remain future compiler/module work and must fail clearly
until implemented.

## Modularity Rule

- Include only what is imported or used.
- Configure only what is configured.
- Initialize only what Plan requires.
- Package only what app references where feasible.

No SQLite import/use means no SQLite provider metadata, no SQLite capability metadata, no
SQLite JS bridge registration, and no SQLite package dependency claim.

## Non-Goals

- No runtime/compiler/provider/HTTP feature implementation in this design-lock PR.
- No public alpha docs.
- No benchmark/performance claims.
- No Node/npm/package-manager compatibility.
- No PostgreSQL/SQL Server JS bridge.
- No ORM/migrations.
- No decorators or controllers in framework MVP.
- No full DI container.
- No native JSON fast path.
- No multi-isolate implementation.
## ENGINE-14 Implementation Note

The framework MVP function-module shape is supported at source level by the compiler-owned
subset: `app.use(sqlite(...))`, `app.useModule(usersModule)`, `app.group(...)`, and literal
`group.get/post/put/patch/delete(...)` registrations are resolved, analyzed, and lowered into
the existing Plan and classic artifact path. This does not mean the bootstrap stdlib exposes a
general runtime implementation of those APIs yet; the current classic bootstrap still executes the
compiler-generated artifact helpers such as `mapGet`/`mapPost` plus provider metadata. Controllers,
decorators, runtime validation, and broader framework examples remain separate framework tasks.
COMPILER-30.E adds compiler metadata for supported config reads, schema declarations, request
bindings, and result helpers; it does not make those bootstrap APIs executable outside the
compiler-generated artifact path.
