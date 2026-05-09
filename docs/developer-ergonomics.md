# Developer Ergonomics

Sloppy's ergonomics model is "predictability over magic." The developer loop is
built around explicit phases and explicit failure.

## Why This Loop Exists

- compile to known artifacts;
- validate plan and startup invariants before dispatch;
- keep runtime boundaries explicit (engine, provider, capability, and packaging
  lanes);
- return diagnostics when behavior is unsupported instead of emulating another
  platform.

This reduces "works by accident" behavior and makes evidence easier to trust.

## Current Shape

`src/main.c` reflects a split command model:

- build/run for execution paths;
- routes/capabilities/doctor/audit/openapi for metadata-oriented inspection.

`src/core/plan_parse.c` and `src/core/app_host.c` enforce the corresponding
runtime model: strict parsing, startup validation, and clear unsupported
diagnostics.

## Evidence-Oriented Ergonomics

Developers get clearer boundaries when evidence lanes stay separate:

- non-V8 success does not imply JS handler execution;
- native provider tests do not imply live environment readiness;
- package layout success does not imply release readiness.

## Documentation Rules

For the current phase, ergonomics docs should describe implemented behavior,
name non-goals directly, and avoid borrowed Node/package-manager assumptions.
