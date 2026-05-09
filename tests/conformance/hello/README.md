# Hello Conformance

Source fixture: `examples/compiler-hello/app.js`.

Build command:

```powershell
cargo run --manifest-path compiler/Cargo.toml -- build examples/compiler-hello/app.js --out <artifacts>
```

Artifact expectations:

- `app.plan.json`;
- `app.js`;
- `app.js.map`;
- byte-identical output across repeated builds;
- no checkout-local paths in emitted artifacts.

Run command when V8 is enabled:

```powershell
sloppy run --artifacts <artifacts> --once GET /
```

Expected output: an HTTP response whose body contains `Hello from Sloppy`.

Gated requirements: execution requires a V8-enabled build. The default non-V8 suite validates
compile/artifact determinism and the clear V8-disabled diagnostic, not handler execution.
