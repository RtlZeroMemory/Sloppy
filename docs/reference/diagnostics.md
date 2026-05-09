# Diagnostics Reference

Diagnostics are emitted by compiler, runtime, Plan parser, and CLI tooling.

## Compiler Diagnostics (`sloppyc`)

- Code format: `SLOPPYC_E_*`
- Include source path/span data when available.
- Rendered by `sloppyc` on compile failure.

Common categories:

- unsupported input/import/route/handler shapes
- invalid typed binding signatures
- configuration read/parse/type errors
- metadata emission errors

## Plan Parser Diagnostics

Plan fixture coverage in `tests/golden/plan` includes parser diagnostic codes such as:

- `SLOPPY_E_MALFORMED_JSON`
- `SLOPPY_E_INVALID_PLAN_VERSION`
- `SLOPPY_E_INVALID_PLAN_FIELD`
- `SLOPPY_E_DUPLICATE_HANDLER_ID`
- `SLOPPY_E_INVALID_ROUTE_PATTERN`

## Runtime/Provider Diagnostics

Current runtime/provider errors include redacted sensitive fields for connection strings, passwords, tokens, and similar secret-bearing values.

Examples from tests:

- unavailable provider feature gates
- cancelled/deadline-exceeded provider operations
- closed-resource and nested-transaction errors

## CLI Command Output Shapes

`routes`, `capabilities`, `doctor`, and `audit` support text and JSON output.

Current `doctor`/`audit` severity/status values in golden outputs:

- `ok`
- `warn`
- `error`
- `note`

## openapi Command Output

`sloppy openapi` emits an OpenAPI 3.0.3 document with Sloppy-specific extension fields such as:

- `x-slop-openapi-policy`
- `x-slop-completeness`
- `x-slop-capabilities`
- `x-slop-optimization-candidates`
- partial markers when schema data is unknown

## Redaction Rules

CLI and provider surfaces apply redaction before writing diagnostics and JSON outputs. Secret-bearing configuration/provider values are represented as `<redacted>`.
