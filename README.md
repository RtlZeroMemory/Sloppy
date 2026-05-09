# Sloppy

Sloppy is a pre-alpha TypeScript backend app runtime and app-host.

It compiles supported source into artifacts (`app.plan.json`, `app.js`,
`app.js.map`), validates those artifacts, and executes handlers through the
native runtime.

Core pieces:

- C runtime kernel for Plan loading, routing, diagnostics, providers, and platform IO.
- Isolated C++ V8 bridge for handler execution.
- Rust `sloppyc` compiler built on Oxc.

## Build From Source (Windows x64)

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

To run handler code, use the V8-enabled lane:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

## First App

From `examples/hello-minimal/src/main.ts`:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
```

Run one request (V8-enabled build):

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples\hello-minimal\src\main.ts --once GET /hello/Ada
```

Expected body:

```json
{"hello":"Ada"}
```

Guided walkthrough: [Tutorial: Build your first Sloppy API](docs/tutorials/first-api.md).

## Command Map

`sloppy` app-host and metadata commands:

```text
sloppy build [source.js|source.mjs|source.ts] [--out <dir>] [--environment <name>] [--host <host>] [--port <port>]
sloppy run [source.js|source.mjs|source.ts|--artifacts <dir>] [--stdlib <dir>] [--environment <name>] [--host <host>] [--port <port>] [--once METHOD TARGET]
sloppy routes --plan <path> [--format text|json]
sloppy capabilities --plan <path> [--format text|json]
sloppy doctor [--plan <path>] [--format text|json]
sloppy audit --plan <path> [--format text|json]
sloppy openapi --plan <path> [--output <path>]
```

`sloppyc` compiler command:

```text
sloppyc build <input.js|input.ts> --out <directory> [--environment <name>] [--host <host>] [--port <port>] [--config-dir <dir>] [--config <key=value>]
```

`sloppy run <source>` compiles first, then runs artifacts.
`sloppy run --artifacts <dir>` runs existing artifacts.
`sloppy build --artifacts ...` is rejected.

Full details: [CLI reference](docs/reference/cli.md).

## Docs Map

- [Docs home](docs/README.md)
- [Tutorials](docs/tutorials/)
- [How-to guides](docs/how-to/)
- [Reference](docs/reference/)
- [Explanation](docs/explanation/)
- [Contributor docs](docs/contributor/)
- [Internals](docs/internals/)

## Boundaries

- Sloppy is pre-alpha and not production-ready.
- Runtime handler execution requires a V8-enabled build.
- Source input is compiled to artifacts before execution.
- Third-party package import resolution is outside the current runtime surface.
- Optional or skipped lanes are not pass evidence.

## Contributing

Human workflow: [CONTRIBUTING.md](CONTRIBUTING.md)
Automation workflow: [AGENTS.md](AGENTS.md), [AGENTS_CONTRIBUTING.md](AGENTS_CONTRIBUTING.md)

## License

[LICENSE.md](LICENSE.md)
