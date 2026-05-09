# Compiler And Plan Model

Sloppy uses a compiler contract, not runtime guesswork.

The compiler emits artifacts; the runtime consumes a strict plan. The plan is
not advisory metadata. It is the startup contract for app-host validation.

`src/core/plan_parse.c` shows the shape of that contract today:

- `schemaVersion` is required and only version `1` is accepted;
- plan sections such as `target`, `bundle`, and `sourceMap` are required and
  typed;
- `requiredFeatures` must be well-formed when present;
- provider and capability metadata are validated as explicit relationships;
- plan fields that look like secret-bearing keys are rejected.

`src/core/app_host.c` then performs runtime-side validation before execution:

- plan target and runtime minimum version must match what the host supports;
- handlers and routes must be internally consistent;
- provider and capability tokens must be valid and cross-referenced.

Generated JavaScript (`app.js`) and plan metadata are both required because they
serve different roles:

- JS registers and runs handlers through the engine bridge;
- the plan gives native code a deterministic graph to validate, introspect, and
  police before dispatch.
