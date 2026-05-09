# Framework v2 Hello Example

This is an executable Framework v2 source-input example for the current compiler/runtime
subset.

It demonstrates a TypeScript source file running through the source-input path,
Plan metadata emission, typed route parameter materialization, and a
`Results.ok(...)` response from V8.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-hello/app.ts --once GET /hello/Ada
```

## Limitations

This example covers one Framework v2 hello route. Package resolution, live
providers, and broader HTTP features are covered by separate examples.
