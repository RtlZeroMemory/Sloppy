# Build Your First Sloppy API

Create a minimal API, build artifacts, and run one request.

## Prerequisites

- `sloppy` is installed. See [Install](../install.md).
- Supported npm platform packages include the runtime needed to execute
  handlers. Source builds need the V8 SDK restored before building that
  runtime.

## Create

```sh
sloppy create hello-api --template minimal-api
cd hello-api
```

Expected result: the project contains `.gitignore`, `sloppy.json`,
`appsettings.json`, and `src/main.ts`.

## Read The App

`src/main.ts` contains a health route and a parameterized hello route:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }));

export default app;
```

## Build

```sh
sloppy build
```

Expected result: `.sloppy/app.plan.json`, `.sloppy/app.js`, and
`.sloppy/app.js.map` are written.

## Run One Request

```sh
sloppy run .sloppy --once GET /hello/Ada
```

Expected body:

```json
{"hello":"Ada"}
```

If this reports that handler execution requires a V8-enabled build, the CLI is
installed but that source-built runtime cannot execute handlers. The build
artifacts remain valid.

## Inspect Routes

```sh
sloppy routes .sloppy
```

Expected result: the route list includes `GET /health` and `GET /hello/{name}`.
