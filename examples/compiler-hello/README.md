# Compiler Hello Example

This is a supported compiler fixture input.
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

With a handler-capable Sloppy build, run the artifacts through the bounded development runtime:

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

This writes generated artifacts under `.sloppy` for positional
source input. The explicit `--artifacts` form remains useful when inspecting a known output
directory or debugging generated files.

## Limitations

This run path requires handler execution support and focuses on compile plus
bounded run flow. Broader TypeScript checking, bundling, HTTPS, request-body
parsing, streaming, middleware, and hot reload are separate work.
