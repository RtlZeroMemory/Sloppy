# Plan Format Reference

Compiler artifact output is:

```text
app.plan.json
app.js
app.js.map
```

`app.plan.json` is parsed by native Plan parsing and validated before runtime execution.

## Required Top-Level Plan Fields

From parser fixtures (`valid-minimal.plan.json`):

- `schemaVersion`
- `compilerVersion`
- `runtimeMinimumVersion`
- `stdlibVersion`
- `target` (`platform`, `engine`)
- `bundle` (`path`, `id`, `hash`)
- `sourceMap` (`path`, `id`, `hash`)
- `handlers` (non-empty array)

## Route Metadata (Optional But Supported)

When present, route entries include:

- `method` (`GET`, `POST`, `PUT`, `PATCH`, `DELETE`)
- `pattern` (Plan route grammar)
- `handlerId`
- optional `name`

Parser rejects invalid methods and invalid route patterns.

## Additional Metadata Sections

Compiler-emitted plans can also include:

- `routes`
- `modules`
- `sourceFiles`
- `dataProviders`
- `capabilities`
- `completeness`
- `strongPlan`
- `features`
- `requiredFeatures`
- `doctorChecks`
- `configuration`
- `schemas`
- `configReads`

## Validation Behavior

Fixture-backed parser behavior:

- unknown future fields are ignored
- known fields with wrong JSON type fail
- duplicate handler IDs fail
- duplicate route method+pattern pairs fail
- duplicate route names fail
- secret-bearing provider/capability metadata is rejected

## Target And Runtime Limits

Current compiler emission sets:

- `target.platform`: `windows-x64`
- `target.engine`: `v8`

Current `sloppy run` validates target engine/platform and runtime minimum version before evaluating handlers.
