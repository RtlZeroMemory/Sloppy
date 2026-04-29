# Tests

This directory is the home for Sloppy's test suites.

No runtime feature tests exist yet because the repository is in the foundation phase. CMake
now registers placeholder CLI smoke tests plus focused C unit tests for core primitives and
diagnostics foundation behavior. The `bootstrap.stdlib.assets` CTest check also verifies
that placeholder bootstrap stdlib assets exist in source and are copied into the build
support-data layout.

Future suites:

- unit tests with munit;
- integration tests for compiled app artifacts;
- golden tests for compiler output and diagnostics;
- fuzz tests for parsers and untrusted input boundaries;
- sanitizer configurations in CI.
