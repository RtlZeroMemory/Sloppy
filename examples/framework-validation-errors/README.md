# Framework Validation Errors Example

A compile-time validation example.

The handler keeps its request-body TypeScript shape visible to `sloppyc`, so the
generated Plan schema metadata can drive native request validation before the
handler is invoked.

## Scope

Negative-path coverage lives in validation conformance tests. Unsupported body
formats are not covered here.
