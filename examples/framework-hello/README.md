# Framework Hello Example

A minimal runnable example for the current compiler/runtime subset.

It shows a TypeScript source file going through the source-input path, Plan
metadata emission, typed route parameter materialization, and a `Results.ok(...)`
response from V8.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-hello/app.ts --once GET /hello/Ada
```

## Scope

This example covers a single hello route. Package resolution, live providers,
and broader HTTP features are covered by separate examples.
