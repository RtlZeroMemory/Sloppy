# Post-ENGINE-16 Diagnostics and Observability Audit

Status: 2026-05-05 planning/consolidation audit. This records diagnostics/observability
state after ENGINE-15 source maps/diagnostics and ENGINE-16 lifecycle diagnostics.

## Source Inputs

- Code: `include/sloppy/diagnostics.h`, `src/core/diagnostics.c`,
  `src/engine/v8/engine_v8.cc`, `src/core/provider_executor.c`,
  `src/platform/libuv/http_transport_libuv.c`, compiler diagnostics, CLI tooling, tests,
  and diagnostic goldens.
- Docs: `docs/diagnostics.md`, `docs/testing-strategy.md`,
  `docs/modules/engine-v8/README.md`, `docs/modules/http/README.md`,
  `docs/modules/data/README.md`, and `docs/modules/app-host/README.md`.

## Current Observability State

| Area | Status | Evidence | Gap |
| --- | --- | --- | --- |
| Stable diagnostic codes | Complete for current code registry | ENGINE-15.CD/E added registry completeness and representative goldens. | Future codes need a registry policy tied to runtime events/counters. |
| Text/JSON/source-frame rendering | Complete for current renderer | Shared renderer can emit deterministic JSON and source frames when source text is available. | CLI-wide diagnostic format plumbing remains deferred. |
| Source maps | Complete for bounded current V8 exception primary spans | Compiler emits Source Map v3 metadata; V8 remaps compile/eval/call exception primary spans with fallback for missing/malformed maps. | Async stack remapping and broader source-frame fidelity remain deferred. |
| V8-gated diagnostics | Honest | Docs/tests separate default non-V8 from V8 remapping/execution lanes. | Roadmap-2 should preserve separate evidence lanes. |
| Redaction | Partial / good coverage | Provider connection strings, config metadata, and executor admission hints have redaction rules/tests. | No single cross-runtime event/redaction registry exists yet. |
| Lifecycle diagnostics | Complete for native helper layer | ENGINE-16.D/E added lifecycle code names, late-completion and leak diagnostics, and JSON coverage. | Timer/provider/callback counter integration is missing. |
| Provider diagnostics | Partial | Native providers have provider-specific errors; provider executor has admission/capability/counter diagnostics. | SQLite executor-backed bridge diagnostics and provider result/resume diagnostics are missing. |
| HTTP diagnostics | Partial | Transport has stable parse/body/timeout/write/keep-alive/chunked/backpressure diagnostics. | Request ID, correlation, access events, and route-level policy diagnostics are missing. |
| Compiler diagnostics | Partial / strong current subset | `sloppyc` emits deterministic diagnostics/goldens for supported subset and avoids panics for user input paths under tests. | Broader TypeScript checking and imported helper diagnostics remain future compiler work. |
| Doctor/audit output | Partial | Strong Plan consumers emit deterministic metadata-derived output. | Runtime live events/counters and route-level policy visibility are not present. |
| Runtime counters/events | Missing as a coherent model | Some counters exist inside HTTP transport, provider executor, resource/app snapshots. | No event/counter registry, no access log model, no test reporting for unified runtime stats. |

## Key Questions Answered

- Diagnostic codes are stable enough for current implemented paths and documented in
  `docs/diagnostics.md`.
- Runtime errors map to author source where currently claimed: bounded V8 exception
  primary-span remapping through compiler source maps. Broader async stack remapping is
  not claimed.
- V8 diagnostics are clearly V8-gated and default gates must not claim them.
- Redaction rules are enforced in important provider/config paths but are not yet a single
  runtime-wide registry.
- Runtime metrics/counters are missing as a coherent observability layer.
- An event/counter registry should land before torture/perf-style work.
- Doctor/audit outputs are deterministic for Plan metadata, but not live runtime
  observability tools.

## Recommended Roadmap-2 Work

- ENGINE-29.A: Runtime Event Model.
- ENGINE-29.B: Counter Registry.
- ENGINE-29.C: HTTP Transport Counters.
- ENGINE-29.D: Provider Executor Counters.
- ENGINE-29.E: Scope/Resource/Lifecycle Counters.
- ENGINE-29.F: CLI/Test Reporting for Runtime Stats.

ENGINE-29 should follow ENGINE-26 and consume the policy decisions from HTTP-26 and
ENGINE-28 where counters/events cross those boundaries.
