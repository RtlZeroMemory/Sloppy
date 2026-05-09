# Dogfood Catalog

This directory does not introduce new runtime behavior. It names the current executable,
gated, and blocked dogfood targets so example evidence can be reported honestly. The
machine-readable catalog is `dogfood.json`.

Current runnable or diagnosable targets:

- `hello-artifact`: uses `examples/compiler-hello/app.js` and the checked-in generated
  artifact fixture under `compiler/tests/fixtures/hello-mapget/expected`.
- `hello-source-input`: uses `examples/hello-minimal/src/main.ts` through the source-input
  fixture harness.
- `package-hello-artifact`: uses the package outside-checkout smoke fixture when a package
  archive is provided.

Feature app targets for HTTP, HTTPS/TLS, SQLite, PostgreSQL, SQL Server, and Framework v2
are cataloged with blocked or gated status until their owning tracks provide the required
runtime and evidence lanes. Skipped or unavailable entries are not pass evidence.

Default non-V8 dogfood runs are allowed to prove clear diagnostics for V8-required
examples. Positive execution of the hello handlers requires a V8-enabled build.
