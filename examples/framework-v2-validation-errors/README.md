# Framework v2 Validation Errors Example

This is a compile-time Framework v2 validation example.
This example keeps the request-body TypeScript shape visible to `sloppyc`, so generated
Plan schema metadata can drive native request validation before a supported handler is
invoked.

## Limitations

Negative-path coverage lives in the validation conformance lane. Unsupported
body formats are outside this example.
