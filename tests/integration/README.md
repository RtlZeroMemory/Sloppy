# Integration Tests

Integration tests verify compiled Sloppy app artifacts, runtime artifact loading, source-map
metadata, and selected V8-gated execution paths through the native host.

Current integration coverage includes handwritten artifact execution, compiler-emitted
artifact execution, HTTP dispatch execution, invalid descriptor failure behavior, source
input handoff through `tests/cmake/check_source_input_run.cmake`, and runtime-artifact
boundary tests that keep bootstrap artifact reads separate from public app filesystem
policy.

Integration evidence is lane-specific. Default non-V8 integration can prove artifact
validation, source-input compiler handoff, and clear unsupported diagnostics. V8-gated
integration proves execution only when the V8 SDK lane is configured. Package
outside-checkout evidence lives in `tools/windows/test-package.ps1` and
`tests/fixtures/package`.
