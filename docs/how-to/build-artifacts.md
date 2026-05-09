# How To Build Artifacts

Build Plan-backed artifacts without starting the runtime server.

## Prerequisites

- `sloppy` and `sloppyc` are installed and on `PATH`.
- Either a `sloppy.json` project or a source entry file (`.js`, `.mjs`, `.ts`).

## Steps

1. Build from a project root that has `sloppy.json`.

```powershell
sloppy build
```

2. Or build from an explicit source input.

```powershell
sloppy build src/main.ts --out .sloppy
```

3. Optional direct compiler invocation.

```powershell
sloppyc build src/main.ts --out .sloppy --environment Development --config-dir .
```

## Expected Result

```text
.sloppy/app.plan.json
.sloppy/app.js
.sloppy/app.js.map
```

`sloppy build` compiles and validates artifacts. It does not enter V8 execution.

## Common Failures

- `sloppy build: --artifacts is not supported; build compiles source input only`.
- `sloppy build: --out applies to positional source input; project build uses sloppy.json outDir`.
- `sloppy build: unsupported source input`: only `.js`, `.mjs`, `.ts` are accepted.
- `sloppy build: --port requires a value from 1 to 65535` or `--host requires an IPv4 address`.
