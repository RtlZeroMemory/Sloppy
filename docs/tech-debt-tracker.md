# Technical Debt Tracker

## Active debt

- V8 SDK fetch/source-build workflow is placeholder; TASK 07.A validates existing SDK roots
  only.
- Sanitizer coverage is incomplete on Windows.
- No real runtime features yet.

## Deferred decisions

- `sloppyc`/Oxc integration is planned but not implemented.
- Linux/macOS CI is future.
- Package compatibility is future.
- Exact event loop backend integration is future.
- HTTP request scope integration with `SlScope` is future.
- Resource-table cleanup callbacks registered through `SlScope` are future.
- Async cancellation and deadline-triggered `SlScope` cleanup are future.
- Exact worker-pool implementation is future.
- DB provider async strategy is future.
- DB transaction scope integration with native lifetime cleanup is future.
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
- Plan file-based loading is future work.
- Plan runtime compatibility checks for target platform, target engine, runtime minimum
  version, and stdlib version are future work.
- Plan hash verification is future validation work.
- Plan source map parsing and JSON pointer/source-frame diagnostics are future work.
- Plan route/service/module/data provider/capability/permission sections are future work.
- Plan route/service/module/data provider/capability/permission golden fixtures are future
  work and should be added only when those sections are implemented.
- Exact V8 SDK source, version pin, checksum manifest, and update cadence are future work.
- Exact V8 source-build workflow, GN args, and SDK packaging matrix are future work.
- Exact V8 dynamic DLL copy/package rules are future work.
- Exact final V8 library list is future work beyond the TASK 07.A minimal family checks.
- V8-backed `SlEngine` creation now exists only for the TASK 07.C smoke path; app bundle
  loading and handler-ID execution remain future work.
- Explicit V8 process shutdown policy is future work; TASK 07.C keeps process-wide V8
  platform state alive after first initialization and releases only per-engine isolates.
- `SlEngine` owner-thread checks are future bridge/event-loop work.
- Real `sl_engine_call_handler` execution and plan handler-ID mapping are future EPIC-08/09
  work.
- V8 source-map remapping is future work; TASK 07.D reports generated JavaScript locations
  only.
- V8 route/handler-aware diagnostics are future EPIC-08/09 work after plan handler mapping
  exists.
- V8 promise rejection policy and async stack traces are future event-loop/promise work.
- Rich V8 code frames are future diagnostics renderer work.
- ESM/module loading and resolver behavior are future work.

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
