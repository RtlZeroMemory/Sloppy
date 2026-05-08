# Framework v2 Hello Example

Status: executable Framework v2 source-input example for the current compiler/runtime
subset.

This example proves a TypeScript source file can run through the dev-only source-input
path, emit Plan metadata, materialize a typed route parameter, and return a deterministic
`Results.ok(...)` response from V8.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-hello/app.ts --once GET /hello/Ada
```

This is not HTTP server production evidence, package-manager behavior, Node/Bun/Deno
compatibility, public alpha documentation, or benchmark evidence.
