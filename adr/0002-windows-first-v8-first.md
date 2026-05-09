# 0002: Windows First, V8 First

## Status

Accepted.

## Context

The primary target is Windows x64. The primary developer environment is Windows with
`clang-cl`, `lld-link`, CMake, Ninja, and Rust/Cargo for the compiler tool.

Sloppy needs a JavaScript engine backend. JavaScriptCore remains interesting, but it is
harder operationally on Windows.

## Decision

Sloppy will target the Windows x64 developer workflow first and use V8 as the first
JavaScript engine backend. The default C toolchain path is `clang-cl` and `lld-link`.

This does not make the core runtime Windows-only. Core code remains cross-platform by
design, with platform-specific API usage isolated under `src/platform/*`.

V8 is treated as a special SDK dependency rather than a normal vcpkg dependency at first.

## Consequences

Windows integration, CI, packaging, and SDK discovery are first-class concerns. Normal
contributors should eventually use prebuilt V8 artifacts. Maintainers may build V8 from
source through separate tooling.

## Alternatives Considered

- JavaScriptCore first: deferred because Windows operation is more difficult.
- Multi-engine support from day one: rejected because it would weaken the initial boundary.
- Interpreter or custom JS engine: rejected because Sloppy needs modern JS/TS execution.

## Follow-up Tasks

- Keep Windows presets and CI green first.
- Keep core runtime portable and platform-specific APIs isolated.
- Define V8 SDK manifest and `SLOPPY_V8_ROOT` layout before adding V8 code.
- Revisit additional engines only after the V8 boundary is validaten.
