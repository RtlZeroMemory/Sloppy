# Tests

This directory is the home for Sloppy's test suites.

No runtime feature tests exist yet because the repository is in the foundation phase. CMake
now registers placeholder CLI smoke tests plus focused C unit tests for core primitives and
diagnostics foundation behavior. The `bootstrap.stdlib.assets` CTest check verifies that
bootstrap stdlib assets exist in source and are copied into the build support-data layout.
The `bootstrap.stdlib.api_shape` CTest check statically verifies the tiny bootstrap
`Results`, `schema`, and `Sloppy` API shape. When `node` is available, CTest also registers
`bootstrap.stdlib.app_host_foundation`, which executes the ESM stdlib and verifies the
bootstrap builder/app freeze model, config, logging, services, route groups, result helper
descriptors, schema validation, route context, and `Sloppy.create()` consistency. This test
is not a Node compatibility claim.
When `node` is available, CTest also registers `bootstrap.stdlib.modules`, which executes
the ESM stdlib and verifies `Sloppy.module`, `builder.addModule`, dependency ordering,
module diagnostics, route/service attribution, and module debug metadata. This test is not
a Node compatibility claim.
The `examples.hello.api_shape`, `examples.ergonomics.api_shape`, and
`examples.modules_basic.api_shape` CTest checks statically verify the public examples use
the current bootstrap API shape without requiring Node, npm, compiler extraction, app-plan
emission, or HTTP runtime behavior.

Future suites:

- unit tests with munit;
- integration tests for compiled app artifacts;
- golden tests for compiler output and diagnostics;
- fuzz tests for parsers and untrusted input boundaries;
- sanitizer configurations in CI.
