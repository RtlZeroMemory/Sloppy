# Public Documentation

Status: pre-alpha skeleton, not published user documentation.

This directory is reserved for developer/user-facing documentation. It should explain the
implemented pre-alpha surface, name current limits clearly, and avoid implying public alpha
readiness before the release gate is complete.

Current files:

- `getting-started.md`: current runnable path and pre-alpha limits;
- `app-model.md`: builder/app model;
- `routing.md`: route declarations and request context;
- `results.md`: result helpers;
- `modules.md`: app modules;
- `config.md`: configuration;
- `logging.md`: logging;
- `services.md`: services;
- `data.md`: data providers;
- `permissions.md`: permissions and capabilities;
- `diagnostics.md`: user-facing diagnostics;
- `cli.md`: CLI commands.

Pages in this directory may describe implemented subsets, intended API shape, and explicit
limitations. They are not public launch docs. Public alpha remains blocked by framework,
HTTP, packaging, example, release-note, and final verification work.

Checked-in examples are split into two groups:

- compiler/runtime examples, such as `examples/compiler-hello/`,
  `examples/request-context/`, and `examples/users-api-sqlite/`, which have scoped
  artifact or V8-gated evidence;
- API-shape examples, such as `examples/hello/`, `examples/ergonomics/`,
  `examples/modules-basic/`, and provider examples, which document current or intended
  source shape without claiming full runtime execution.

Every public page must preserve these boundaries:

- no production-readiness claim;
- no benchmark or performance claim;
- no Node/Bun/Deno compatibility claim;
- no package-manager claim;
- no public alpha release claim;
- no tutorial that depends on Framework v2 or packaging work that has not landed.
