# Source Input And Artifacts

Sloppy treats source input and runtime input as two different phases on purpose.
The runtime does not execute a source tree directly. It executes compiler
artifacts.

```text
src/main.ts
  -> sloppyc
  -> app.plan.json
  -> app.js
  -> app.js.map
  -> sloppy runtime
```

Why this exists:

- the compiler can emit structured metadata in `app.plan.json`;
- `src/core/plan_parse.c` can validate that metadata strictly before runtime
  work starts;
- `src/core/app_host.c` can reject invalid targets, incompatible runtime
  versions, duplicate handler IDs, malformed route/provider/capability
  sections, and missing handler references during startup;
- CLI introspection commands can inspect plan metadata without pretending that
  handler execution already happened.

`src/main.c` keeps this boundary explicit. `run` can accept source input, but it
still goes through compile-then-run. Artifact input remains first-class and can
be inspected separately.

This model is also why Sloppy can fail early on malformed plans or unsupported
metadata instead of delaying failures until request execution.
