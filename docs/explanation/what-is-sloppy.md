# What Is Sloppy

Sloppy is a pre-alpha backend runtime and app-host for a bounded TypeScript and
JavaScript subset. It is built from three parts that deliberately stay separate:

- a C runtime and app-host that own plan validation, diagnostics, lifecycle,
  and native boundaries;
- a Rust compiler (`sloppyc`) that emits runtime artifacts;
- an isolated C++ V8 bridge used only when V8 is explicitly enabled.

The product idea is to make backend app shape visible before serving requests:
compile first, validate first, execute known artifacts. The app-host ergonomics
are meant to feel familiar to backend developers: create an app, register
routes and providers, return results. Sloppy adds a stricter compilation and
Plan step so the runtime can reason about routes, handlers, providers, and
capabilities before handler code runs.

Current source shows this boundary explicitly:

- `src/main.c` routes CLI behavior through `build`, `run`, `routes`,
  `capabilities`, `doctor`, `audit`, and `openapi` commands.
- `src/core/plan_parse.c` enforces a strict plan contract instead of permissive
  best-effort loading.
- `src/core/app_host.c` validates startup invariants before request execution.
- `src/engine/v8/*` keeps V8 internals isolated behind Sloppy-owned APIs.

Sloppy currently runs Sloppy-built artifacts. Node-style dependency resolution,
`node_modules`, production operations, and package-manager app dependencies may
come later, but they are not part of the current pre-alpha runtime.
