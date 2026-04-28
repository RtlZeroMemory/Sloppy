# Technical Debt Tracker

## Active debt

- V8 SDK workflow is placeholder.
- Sanitizer coverage is incomplete on Windows.
- No real runtime features yet.

## Deferred decisions

- `sloppyc`/Oxc integration is planned but not implemented.
- Linux/macOS CI is future.
- Package compatibility is future.
- Exact event loop backend integration is future.
- Exact worker-pool implementation is future.
- DB provider async strategy is future.
- Multiple worker/process scaling model is future.
- Cancellation semantics by provider are future.
- Docs freshness automated checker is lightweight and should become more semantic over time.
- Public docs example test runner is future.
- Golden update workflow is future.
- Docs link checker is future.
- Diagnostics JSON output is future.
- Diagnostics source-frame rendering is future.
- Diagnostics source-map integration is future.
- Diagnostics localization is future.
- Diagnostics structured fixes/metadata are future.
- Diagnostics redaction policy is future; TASK 04.A only provides an explicit
  `<redacted>` placeholder helper.

## Cleanup candidates

- Add scanner fixtures or self-test mode for structural checks.
- Decide whether `include/sloppy/os.h` is public or internal before platform APIs grow.
- Expand docs freshness checks to catch broken links and implemented API drift.

## Overengineering Watchlist

- Watch for unnecessary registries.
- Watch for macro DSLs.
- Watch for provider/vtable abstractions before provider phase.
- Watch for generic containers before concrete needs.
- Watch for C code that hides cleanup/error paths.

## Comment Quality Watchlist

- Watch for AI-noise comments.
- Watch for stale rationale comments.
- Watch for missing ownership comments on public APIs.
- Watch for tricky C code without invariants documented.

## Repeated review findings

- None recorded yet.

## Proposed mechanical checks

- V8 leakage scanner expansion once the bridge exists.
- Allocator/resource misuse checks once allocator and resource-table modules exist.
- Docs drift checks for roadmap/source-doc links.
- Public docs example tests once public API exists.
- Golden update intent check once golden harnesses exist.
- Diagnostic snapshot update intent check once more diagnostic fixtures exist.
- Complexity warnings for one-call-site abstractions and high nesting if a reliable scanner
  becomes practical.

## Completed cleanup

- None recorded yet.
