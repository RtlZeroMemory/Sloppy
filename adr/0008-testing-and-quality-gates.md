# 0008: Testing And Quality Gates

## Status

Accepted.

## Context

Sloppy should not accumulate feature code before standards, tests, diagnostics, and quality
gates exist. The runtime will handle untrusted input and native resources, so safety testing
must start early.

## Decision

Testing is first-class from the beginning. The project will use C unit tests, compiler
goldens, diagnostic snapshots, integration tests, fuzz tests, sanitizer tests, static
analysis, and benchmarks.

Feature work should not land without appropriate tests or spec updates. CI enables
warnings-as-errors.

## Consequences

Early development may feel slower, but the project avoids rewriting unstable foundations.
Parser and resource lifetime work must include fuzzing and lifetime tests as soon as those
modules exist.

## Alternatives Considered

- Add tests after the foundation slice: rejected because foundation code is not throwaway.
- Only rely on integration tests: rejected because memory/resource bugs need narrower tests.

## Follow-up Tasks

- Add C unit test framework before core primitive implementation grows.
- Add diagnostics snapshots before diagnostics become user-facing.
- Add compiler golden tests before real artifact emission.
- Add sanitizer/fuzz gates when parsers and allocators exist.
