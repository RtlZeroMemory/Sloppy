# Tests

This directory is the home for Sloppy's test suites.

The repository is still pre-alpha, but it now has unit, integration, conformance, package
smoke, compiler fixture, and static example checks for the implemented foundation slices.
CMake also registers CLI metadata/failure checks plus focused C unit tests for core
primitives and diagnostics behavior. The `bootstrap.stdlib.assets` CTest check verifies
that bootstrap stdlib assets exist in source and are copied into the build support-data
layout.
The `bootstrap.stdlib.api_shape` CTest check statically verifies the tiny bootstrap
`Results`, `schema`, `data`, `sql`, and `Sloppy` API shape. When `node` is available,
CTest also registers `bootstrap.stdlib.import_graph`, which imports each bootstrap ESM
leaf and selected internal helper modules to catch broken split-module paths without
depending on Node compatibility. When `node` is available, CTest also registers
`bootstrap.stdlib.app_host_foundation`, which executes the ESM stdlib and verifies the
bootstrap builder/app freeze model, config, logging, services, route groups, result helper
descriptors, schema validation, route context, and `Sloppy.create()` consistency. This test
uses Node only as a bootstrap ESM host.
When `node` is available, CTest also registers `bootstrap.stdlib.modules`, which executes
the ESM stdlib and verifies `Sloppy.module`, `builder.addModule`, dependency ordering,
module diagnostics, route/service attribution, and module debug metadata. This test uses
Node only as a bootstrap ESM host.
When `node` is available, CTest also registers `bootstrap.stdlib.data_foundation`, which
executes the ESM stdlib and verifies database capability metadata, query template lowering,
fake data provider behavior, transaction callback semantics, and module/service
integration. This test uses Node only as a bootstrap ESM host.
CTest also registers `data.sqlite.provider`, which executes the native SQLite provider
against `:memory:` databases and covers open/close, exec/query/queryOne, parameter binding,
transactions, and diagnostics.
The `bootstrap.stdlib.codec_properties` CTest check runs deterministic property coverage
for Base64/Base64Url/Hex/UTF-8/Binary roundtrips, invalid input diagnostics, and embedded
NUL preservation. This is default-safe fuzz/property evidence, not long fuzzing.
The `examples.hello.api_shape`, `examples.ergonomics.api_shape`,
`examples.modules_basic.api_shape`, `examples.data_foundation.api_shape`,
`examples.sqlite_basic.api_shape`, `examples.redis.api_shape`, `examples.time.api_shape`,
`examples.crypto.api_shape`, `examples.net.api_shape`, `examples.os.api_shape`, and
`examples.config.api_shape` CTest checks statically verify public examples use the current
documented API shape without requiring Node, npm, compiler extraction, app-plan emission,
JavaScript provider connections, external network access, or unrelated runtime behavior.

The governance lane includes `tools/windows/check-test-governance.ps1`, source-input
fixture metadata executed by `tools/windows/test-source-input-fixtures.ps1`, package
outside-checkout fixture metadata with prebuilt artifact evidence, cross-API conformance
indexing, V8 bridge test templates, resource/lifecycle conformance aliases, and
deterministic fuzz seed replay under `tests/fuzz`. libFuzzer, sanitizer, long fuzz,
stress/torture, live-provider, package, V8-gated, and benchmark lanes remain separately
reported.
