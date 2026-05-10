# Sloppy vs Node, Bun, and Deno

Sloppy is not a compatibility layer for Node, Bun, or Deno. It explores a
different shape: compiler-first TypeScript apps with a native runtime and
first-party backend APIs.

Node, Bun, and Deno run JavaScript programs. Sloppy compiles supported
TypeScript source into a Plan first, then runs the known app shape on a native
runtime. That makes Sloppy stricter in some places, but it gives the runtime
and tooling more information before execution.

## The short version

Node.js, Bun, and Deno are general JavaScript runtimes. They are good choices
when you need the JavaScript ecosystem, dynamic program structure, existing
frameworks, or mature deployment paths.

Sloppy is a compiler-first backend runtime and app host. A Sloppy app imports
the Sloppy stdlib, stays inside the compiler-supported source subset, and
emits artifacts that the runtime validates before serving work:

- `app.plan.json` — route, handler, capability, provider, config, response,
  and artifact metadata;
- `app.js` — generated bundle;
- `app.js.map` — source map for diagnostics.

Sloppy favors first-party backend/runtime APIs over arbitrary npm app
dependencies today. The trade is direct: less dynamic freedom, more app
metadata available before the first request.

## A tiny route in each runtime

These examples intentionally use each runtime's built-in surface, not a
third-party framework.

### Sloppy

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));
app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

Build and run:

```sh
sloppy run src/main.ts --once GET /hello/Ada
```

### Node.js

```js
import http from "node:http";

const server = http.createServer((req, res) => {
  const url = new URL(req.url, "http://localhost");

  if (req.method === "GET" && url.pathname === "/health") {
    res.writeHead(200, { "content-type": "text/plain; charset=utf-8" });
    res.end("ok");
    return;
  }

  const match = url.pathname.match(/^\/hello\/([^/]+)$/);
  if (req.method === "GET" && match) {
    res.writeHead(200, { "content-type": "application/json" });
    res.end(JSON.stringify({ hello: decodeURIComponent(match[1]) }));
    return;
  }

  res.writeHead(404);
  res.end();
});

server.listen(5173, "127.0.0.1");
```

Run:

```sh
node server.js
```

### Bun

```js
Bun.serve({
  port: 5173,
  fetch(request) {
    const url = new URL(request.url);

    if (request.method === "GET" && url.pathname === "/health") {
      return new Response("ok", {
        headers: { "content-type": "text/plain; charset=utf-8" },
      });
    }

    const match = url.pathname.match(/^\/hello\/([^/]+)$/);
    if (request.method === "GET" && match) {
      return Response.json({ hello: decodeURIComponent(match[1]) });
    }

    return new Response(null, { status: 404 });
  },
});
```

Run:

```sh
bun server.js
```

### Deno

```ts
Deno.serve({ port: 5173 }, (request) => {
  const url = new URL(request.url);

  if (request.method === "GET" && url.pathname === "/health") {
    return new Response("ok", {
      headers: { "content-type": "text/plain; charset=utf-8" },
    });
  }

  const match = url.pathname.match(/^\/hello\/([^/]+)$/);
  if (request.method === "GET" && match) {
    return Response.json({ hello: decodeURIComponent(match[1]) });
  }

  return new Response(null, { status: 404 });
});
```

Run:

```sh
deno run --allow-net server.ts
```

## What Sloppy knows before runtime

In Node, Bun, and Deno, framework metadata usually comes from executing app
registration code or from framework-specific tooling. Sloppy's compiler records
the supported app shape in the Plan.

For the route above, a current Plan contains fields like this. This excerpt is
illustrative, but it uses current field names:

```json
{
  "schemaVersion": 1,
  "kind": "web",
  "routes": [
    {
      "method": "GET",
      "pattern": "/hello/{name}",
      "handlerId": 2,
      "response": {
        "kind": "json",
        "status": 200
      },
      "responses": [
        {
          "status": 200,
          "kind": "json",
          "contentType": "application/json"
        }
      ]
    }
  ],
  "requiredFeatures": ["stdlib"]
}
```

Depending on the source shape, the Plan can also carry:

- handler IDs and source locations;
- route params, query/header/body bindings, and request context usage;
- config reads and literal defaults;
- required runtime features such as filesystem, network, OS, time, crypto,
  codec, workers, and provider features;
- provider and capability metadata;
- response metadata visible from `Results.*`;
- OpenAPI-relevant operation metadata;
- metadata completeness markers.

The runtime loads and validates that metadata before handler execution. CLI
commands can also read it without entering V8.

## Developer ergonomics

Sloppy can feel better when you want the runtime and tools to understand the
app shape up front:

- one first-party app surface: `Sloppy.create()`, route registration,
  middleware, services, config, logging, capabilities, and `Results`;
- route metadata without runtime introspection;
- OpenAPI from Plan metadata;
- Plan-backed commands such as `sloppy routes`, `sloppy capabilities`,
  `sloppy doctor`, `sloppy audit`, and `sloppy openapi`;
- source input with `sloppy run src/main.ts`;
- artifact runs with `sloppy run .sloppy`;
- local packages with `sloppy package`;
- Program Mode for route-free tools that use the Sloppy stdlib.

The costs are real:

- Sloppy apps do not resolve arbitrary npm app dependencies today.
- Node built-ins and globals exist only when Sloppy implements an explicit
  API for that behavior.
- The compiler accepts a focused TypeScript/JavaScript source subset.
- Dynamic web app shapes fail closed instead of producing partial route
  metadata.
- Sloppy is pre-alpha. APIs, artifact formats, and internal boundaries can
  still change.

Use this rule of thumb: Sloppy is a good fit when you want compiler-first
backend metadata and can stay inside Sloppy's explicit runtime boundary.
Node, Bun, and Deno are better fits when you need the full JavaScript
ecosystem, maximum dynamic freedom, or mature production deployment today.

## CLI ergonomics

Sloppy's CLI reads source, artifacts, and packages through the same Plan
contract:

```sh
sloppy run src/main.ts
sloppy routes .sloppy
sloppy openapi .sloppy --output openapi.json
sloppy package src/main.ts
```

What each command reads:

| Command | Reads | Purpose |
| --- | --- | --- |
| `sloppy run src/main.ts` | source, then emitted artifacts | compile, validate, and execute |
| `sloppy run .sloppy` | `app.plan.json`, `app.js`, `app.js.map` | validate and execute existing artifacts |
| `sloppy routes .sloppy` | Plan | list route metadata without V8 |
| `sloppy openapi .sloppy --output openapi.json` | Plan | generate OpenAPI JSON for web Plans |
| `sloppy package src/main.ts` | source, then artifacts | create a local package directory |

Node, Bun, and Deno run files directly and have strong ecosystem tooling. They
do not have Sloppy's Plan-backed metadata commands by default because there is
no Sloppy Plan. Frameworks and external tools can add similar workflows, but
that metadata is framework-specific rather than emitted by the runtime's own
compiler step.

## Program Mode

Sloppy is not only for web routes. Program Mode runs route-free source as a
console-style program:

```ts
export async function main(args, ctx) {
    console.log(`hello ${args[0] ?? "world"}`);
}
```

Run it with arguments after `--`:

```sh
sloppy run src/main.ts -- Ada
```

Program Mode is for Sloppy stdlib imports, Plan-backed packaging, and runtime
metadata. It is not Node compatibility. There is no `process` global, Node
built-in module loading, npm resolution, FFI, or raw terminal API in the
current Program Mode surface.

Node, Bun, and Deno are more general-purpose program runners. Sloppy Program
Mode is useful when the tool should use the same compiler/runtime/package
boundary as a Sloppy app.

## Comparison table

| Area | Sloppy | Node.js | Bun | Deno |
| --- | --- | --- | --- | --- |
| Runtime model | Compiler-first app Plan + native runtime | General JavaScript runtime | General JavaScript runtime and toolkit | Secure-by-default JavaScript/TypeScript runtime |
| Package ecosystem | First-party stdlib for the current app model | Mature npm ecosystem | Strong npm support | npm support plus URL/import-map workflows |
| Web metadata | First-party Plan, routes, OpenAPI | App/framework-specific | App/framework-specific | App/framework-specific |
| Dynamic JavaScript freedom | Intentionally limited in Web Mode | High | High | High |
| Program/CLI apps | Program Mode via Sloppy stdlib | Mature | Mature | Mature |
| Node built-ins | Only explicit Sloppy APIs | Native surface | Broad support | Available through compatibility surfaces where supported |
| Production maturity | Pre-alpha | Mature | Mature | Mature |

## When to use Sloppy

Use Sloppy if:

- you want compiler-first backend metadata;
- you want an integrated CLI, app host, and first-party stdlib;
- you like ASP.NET Minimal API-style route ergonomics;
- you want to experiment with Plan-backed tooling;
- you can build inside Sloppy's explicit runtime boundary.

Use Node, Bun, or Deno if:

- you need a mature production runtime today;
- you need npm packages inside the app today;
- you need Node built-ins or framework compatibility;
- you need arbitrary dynamic JavaScript behavior;
- you are porting an existing JavaScript backend without rewriting its runtime
  assumptions.

## Related reading

- [Compiler-first runtime](compiler-first-runtime.md)
- [Why no `node_modules`?](why-no-node-modules.md)
- [The Plan model](../guide/plan-model.md)
- [Plan format](../reference/plan-format.md)
- [Stability reference](../reference/stability.md)
