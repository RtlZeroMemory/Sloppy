# Compiler Hello Example

Status: supported compiler fixture input.

This example is the first Sloppy source shape that `sloppyc build` can compile:

From the repository root:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out .sloppy
```

From this example directory:

```powershell
cargo run --manifest-path ..\..\compiler\Cargo.toml -- build app.js --out .sloppy
```

The command emits:

- `.sloppy/app.plan.json`;
- `.sloppy/app.js`;
- `.sloppy/app.js.map`.

With a V8-enabled Sloppy build, run the artifacts through the bounded development runtime:

From repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy --host 127.0.0.1 --port 5173
```

Expected URL:

```text
http://127.0.0.1:5173/
```

For deterministic smoke tests without opening a socket:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run --artifacts .sloppy --once GET /
```

Expected body:

```text
Hello from Sloppy
```

The source-input shortcut performs the compile step and then enters the same artifact
runtime path:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run app.js --once GET /
```

From the repository root:

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/compiler-hello/app.js --once GET /
```

This writes generated artifacts under `.sloppy/cache/dev/source-input` for positional
source input. The explicit `--artifacts` form remains useful when inspecting a known output
directory or debugging generated files.

This run path is dev-only and requires V8. It does not use Node/npm/package-manager
behavior, does not claim full TypeScript checking or broad bundling, and does not include
production server hardening, HTTPS, request body parsing, streaming, middleware, hot reload,
or Node compatibility.
