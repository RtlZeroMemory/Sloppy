# Package Fixtures

Package fixtures describe the `package outside-checkout` lane. They validate extracted
archives, not a repository checkout.

Package smoke tests validate:

- the runtime loads committed artifacts instead of silently compiling source;
- Plan/source-map files are present and read from the extracted archive;
- bootstrap artifact reads are separate from public app filesystem permissions;
- diagnostics are stable and redacted;
- V8-enabled package execution is a separate V8-gated package run.

When a fixture declares `mustNotCompileSource=true`, `tools/windows/test-package.ps1` must
run the named `prebuiltArtifactFixture` and must not invoke `sloppyc` as part of package
artifact evidence. Compiler-from-package smoke is a separate tooling check.

Release readiness uses separate release lanes.
