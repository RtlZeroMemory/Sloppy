# Stability Reference

Sloppy is pre-alpha. Contracts and behavior can change between revisions.

## Version Fields in Current Artifacts

Current emitted/compiled version markers:

- runtime CLI version string: `0.0.0-foundation`
- compiler version: `sloppyc-0.8.0`
- plan runtime minimum version: `0.1.0`
- plan stdlib version: `0.1.0`

## Plan Schema Stability

Current native parser contract is `schemaVersion: 1`.

Parser behavior:

- unsupported schema versions are rejected
- required fields and known field types are strict
- unknown extra fields are ignored

## Compatibility Checks Enforced at Run Time

`sloppy run` validates artifact compatibility metadata before handler execution:

- target platform/engine fields
- runtime minimum version
- route/handler consistency and related startup validation

## Provider and Runtime Stability

- Provider metadata supports `sqlite`, `postgres`, `sqlserver`.
- Compiler-generated executable provider bridge is sqlite-only.
- Live provider checks are opt-in and environment-dependent.

## Current Limits

- Production hardening is still future work.
- Application dependency workflows are still future work.
- Default local checks do not provide full cross-platform parity coverage.
