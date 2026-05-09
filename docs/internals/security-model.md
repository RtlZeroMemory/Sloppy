# Security Model Internals

## Where It Lives

- `src/core/diagnostics.c`
- `src/core/features.c`
- `src/core/request_validation.c`
- `src/data/*`
- `tests/golden/diagnostics/**`

## Model

Security-sensitive behavior is expressed as concrete boundaries: redaction,
path normalization, capability checks, validation failures, provider
credential handling, and native boundary ownership.

## Limits

Sloppy does not claim OS sandboxing, production hardening, or complete security
coverage. Security claims require specific tests and evidence lanes.
