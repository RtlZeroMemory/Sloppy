# Framework v2 Validation Errors Example

This is a compile-time Framework v2 validation example.
This example keeps the request-body TypeScript shape visible to `sloppyc`, so generated
Plan schema metadata can drive native request validation before a supported handler is
invoked. The example is checked as source-input compile/tooling evidence; it is not a
public HTTP validation contract for unsupported body formats.

Negative-path coverage lives in the validation conformance lane. This example must not
include secrets, production-readiness claims, package-manager behavior, or benchmark
claims.
