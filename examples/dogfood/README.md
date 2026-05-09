# Dogfood Catalog

This directory is a catalog of current dogfood targets. The machine-readable source is
`dogfood.json`.

Current runnable or diagnosable targets:

- `hello-artifact`: uses `examples/compiler-hello/app.js` and the checked-in generated
  artifact fixture under `compiler/tests/fixtures/hello-mapget/expected`.
- `hello-source-input`: uses `examples/hello-minimal/src/main.ts` through the source-input
  fixture harness.
- `package-hello-artifact`: uses the package outside-checkout smoke fixture when a package
  archive is provided.

Feature app targets for HTTP, HTTPS/TLS, SQLite, PostgreSQL, SQL Server, and Framework v2
are listed with their current blocked or gated status until the required runtime support is
available.

Default non-V8 dogfood runs validate clear diagnostics for V8-required
examples. Positive execution of the hello handlers requires a V8-enabled build.
