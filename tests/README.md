# Tests

This directory is the home for Sloppy's test suites.

The repository is still pre-alpha, but it now has unit, integration, conformance, package
smoke, compiler fixture, and static example checks for the implemented foundation slices.
CMake also registers CLI metadata/failure checks plus focused C unit tests for core
primitives and diagnostics behavior. The `bootstrap.stdlib.assets` CTest check verifies
that bootstrap stdlib assets exist in source and are copied into the build support-data
layout.
The `bootstrap.stdlib.api_shape` CTest check statically verifies the tiny bootstrap
`Results`, `schema`, `data`, `sql`, and `Sloppy` API shape. When `node` is available, CTest also registers
`bootstrap.stdlib.app_host_foundation`, which executes the ESM stdlib and verifies the
bootstrap builder/app freeze model, config, logging, services, route groups, result helper
descriptors, schema validation, route context, and `Sloppy.create()` consistency. This test
is not a Node compatibility claim.
When `node` is available, CTest also registers `bootstrap.stdlib.modules`, which executes
the ESM stdlib and verifies `Sloppy.module`, `builder.addModule`, dependency ordering,
module diagnostics, route/service attribution, and module debug metadata. This test is not
a Node compatibility claim.
When `node` is available, CTest also registers `bootstrap.stdlib.data_foundation`, which
executes the ESM stdlib and verifies database capability metadata, query template lowering,
fake data provider behavior, transaction callback semantics, and module/service
integration. This test is not a Node compatibility claim.
CTest also registers `data.sqlite.provider`, which executes the native SQLite provider
against `:memory:` databases and covers open/close, exec/query/queryOne, parameter binding,
transactions, and diagnostics.
The `examples.hello.api_shape`, `examples.ergonomics.api_shape`,
`examples.modules_basic.api_shape`, `examples.data_foundation.api_shape`,
`examples.sqlite_basic.api_shape`, `examples.time.api_shape`, and
`examples.crypto.api_shape` CTest checks statically verify public examples use the current
documented API shape without requiring Node, npm, compiler extraction, app-plan emission,
JavaScript provider connections, or unrelated runtime behavior.

Future suites:

- fuzz tests for parsers and untrusted input boundaries;
- sanitizer configurations in CI.
