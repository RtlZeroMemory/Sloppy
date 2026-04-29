# MAIN and MAIN.1 Scope

Status: locked planning taxonomy for the alpha path.

This document defines the scope labels used by the MAIN and MAIN.1 roadmaps. It does not
implement runtime/compiler/provider behavior and does not mutate GitHub issues.

## Taxonomy

| Classification | Meaning | Examples |
| --- | --- | --- |
| MAIN-critical | Required to prove the minimal supported alpha path works end to end. | `sloppyc build` for a tiny app, V8-enabled `sloppy run --artifacts`, GET route dispatch, text/JSON response descriptors, basic route/query context, clear unsupported-path diagnostics. |
| MAIN.1-hardening | Required to turn MVP/skeleton systems on the supported path into alpha-production quality. | source-input run handoff, resource table, capability enforcement, diagnostics JSON/source frames, JS-native SQLite bridge, provider hardening, conformance suite. |
| post-alpha-deferred | Important later work that is not needed to honestly ship the narrow alpha path. | production HTTP server, middleware pipeline, full TypeScript checking, package-manager behavior, Node compatibility, hot reload, multi-worker scaling. |
| hide/remove/public-docs-off | Things that should not be promoted publicly until they are true. | public SQLite tutorial before JS-native SQLite works, public security claims before enforcement, performance comparisons before comparable benchmarks. |
| already-complete/do-not-duplicate | Work already merged and tested for its scoped MVP/foundation goal. | EPIC-21 through EPIC-26 MVP/foundation work, plus older closed child tasks. |

## ROADMAP MAIN

Purpose: prove the minimal end-to-end alpha path through the intended architecture.

MAIN is not a broad framework hardening pass. It is the smallest honest path where a
developer can write a tiny supported Sloppy app, compile it into artifacts, run those
artifacts locally with a V8-enabled host, receive HTTP responses, and understand clear
boundaries for unsupported behavior.

### Alpha-Supported Workflow

The MAIN-supported workflow is:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy
sloppy run --artifacts .sloppy --stdlib <bootstrap-stdlib-root> --once GET /
sloppy run --artifacts .sloppy --stdlib <bootstrap-stdlib-root>
```

`sloppy run <source.js>` is not part of MAIN unless a later approved MAIN task explicitly
adds source-input handoff. It is a MAIN.1 hardening item because the artifact boundary is
already the intended architecture and is sufficient for the minimal compile-and-run proof.

### What Must Work

- The tiny supported source shape compiles with `sloppyc build`.
- `app.plan.json`, `app.js`, and `app.js.map` are emitted deterministically.
- The generated artifact directory loads through `sloppy run --artifacts`.
- A V8-enabled build loads the classic bootstrap runtime asset.
- Handlers register through the internal runtime intrinsic.
- GET routes dispatch by compiler-emitted route metadata and numeric handler ID.
- Supported `Results.text`, `Results.json`, `Results.empty`, and `Results.problem`
  descriptors serialize to HTTP responses.
- Minimal route/query/request context reaches handlers.
- Unsupported behavior fails clearly.
- Docs say which checks prove default non-V8 behavior and which checks prove V8 behavior.

### What May Remain Minimal

- The server may remain dev-only and single-process.
- The supported compiler input may remain one tiny JS file with literal routes.
- `app.js.map` may remain placeholder if documented honestly.
- Route metadata may remain an interim artifact section.
- Default CI may remain non-V8 as long as optional V8 validation is reported separately.
- Package tooling may remain experimental local packaging.

### Explicitly Deferred From MAIN

- Source-input `sloppy run <source.js>` handoff/cache/watch.
- True V8 ESM module graph loading.
- Production HTTP server hardening, streaming parser state, bodies, middleware, files,
  cookies, redirects, compression, keep-alive policy, and TLS.
- Broad TypeScript syntax, type checking, npm/package resolution, bundling, and Node
  compatibility.
- Native app graph validation for modules/services/data providers.
- JS-to-native database bridge.
- Capability enforcement beyond clear metadata/docs.
- Public alpha marketing docs.

### MAIN Exit Criteria

- The supported `sloppyc build` plus `sloppy run --artifacts` hello path is demonstrated or
  tracked as the final MAIN verification task.
- The issue tracker cleanup plan is approved before creating new roadmap issues.
- MAIN staged issue data contains no duplicate tasks for already-completed EPIC-21 through
  EPIC-26 work.
- README and roadmap docs do not overclaim production readiness, Node compatibility,
  package-manager behavior, security enforcement, live database support, or performance.
- All available validation commands are run or explicitly reported as not run.

## ROADMAP MAIN.1

Purpose: harden the existing MVP/skeleton systems after MAIN works.

MAIN.1 is the alpha-production hardening roadmap. It turns the supported path and adjacent
semi-supported surfaces into something trustworthy for a private/public alpha without
pretending Sloppy is enterprise-production complete.

## Alpha-Production Definition

Alpha-production means:

- the supported path works end to end;
- functionality is real, not fake;
- diagnostics are usable;
- docs are honest;
- tests cover intended behavior;
- unsupported behavior fails clearly;
- skeletons in the supported path are hardened enough to trust.

It does not mean:

- feature completeness;
- public package-manager distribution;
- Node/Bun/Deno compatibility;
- production security sandboxing;
- broad database feature parity;
- performance leadership claims.

### What Barebones Systems Must Become Real

- Source-input `sloppy run` handoff to `sloppyc`.
- Plan compatibility, hashes, source-map and route/module/provider/capability validation.
- Runtime app-host startup validation.
- Resource table and JS-native handle IDs.
- JS-to-native SQLite bridge.
- Runtime capability enforcement.
- Diagnostics JSON/source frames/redaction.
- CLI introspection tied to real compiler/app-host metadata.
- Cross-platform package smoke and optional V8/provider CI gates.
- End-to-end conformance suite for supported examples.

### Advanced Features Required Before Alpha-Production

- Capability-denied diagnostics and audit fixtures.
- Request context limits and unsupported-body/header diagnostics.
- Provider live-test methodology and explicit skip reporting.
- Benchmark methodology that avoids public claims.
- Public docs that only document executable verified workflows.

### What Can Remain Post-Alpha

- Production HTTP feature breadth.
- True ESM module graph beyond the classic bootstrap runtime if the alpha path remains
  artifact-classic and honestly documented.
- Full TypeScript checking and broad syntax support.
- PostgreSQL/SQL Server JS bridges if SQLite is the only alpha-supported database bridge.
- Native plugin ABI.
- Package-manager integration.
- OS sandboxing.
- Hot reload/watch mode.
- Multi-worker/process scaling.

### MAIN.1 Exit Criteria

- Every supported public workflow has an executable test or an explicitly documented
  reason why it remains unsupported.
- Capability/security docs match runtime enforcement.
- JS-native resources use IDs/generation checks, not raw pointers.
- Diagnostics are clear enough for developers and machine-readable where required.
- Public alpha docs are published only after the executable hello and selected data story
  are true or explicitly deferred.
- New GitHub issues are created from deduped staged data only after review.
