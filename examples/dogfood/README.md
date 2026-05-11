# Contributor/Internal Evidence Catalog

This contributor/internal directory is a machine-readable catalog of current
runtime evidence targets. The source is `dogfood.json`.

Current runnable or diagnosable targets:

- `hello-artifact`: uses `examples/compiler-hello/app.js` and the checked-in generated
  artifact fixture under `compiler/tests/fixtures/hello-mapget/expected`.
- `hello-source-input`: uses `examples/hello-minimal/src/main.ts` through the source-input
  fixture harness.
- `prealpha-control-plane`: uses `examples/prealpha-control-plane/` through project-mode
  source input and the app test host coverage suite.
- `package-hello-artifact`: uses the package outside-checkout smoke fixture when a package
  archive is provided.

Feature app targets for HTTP, HTTPS/TLS, SQLite, PostgreSQL, SQL Server, and the typed
framework surface are listed with their current blocked or gated status until the required
runtime support is available.

Default non-V8 runs validate clear diagnostics for V8-required examples.
Positive execution of the hello handlers and control-plane example requires a
V8-enabled build.
