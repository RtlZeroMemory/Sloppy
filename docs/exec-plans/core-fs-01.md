# CORE-FS-01 Execution Plan

Status: active sequential PR train.

## Order

1. CORE-FS-01.A/B: source of truth, feature id, Plan/capability policy.
2. CORE-FS-01.C/D/H: resolver, backend, core operations, JS/V8 surface.
3. CORE-FS-01.E/F: advanced operations, FileHandle, streams.
4. CORE-FS-01.G: watch API.
5. CORE-FS-01.I/J: diagnostics, doctor/audit, conformance, examples, documentation.

## Rules

- Branch each slice from fresh `origin/main`.
- Do not stack PRs.
- Run local gates before opening and before merging.
- Poll GitHub review state after opening, fix clear actionable/blocking feedback, and
  merge only when local gates pass and no unresolved actionable review remains.
- Keep default, V8-gated, package, live-provider, stress/torture, and benchmark evidence
  lanes separate.

## Current Slice

CORE-FS-01.G is limited to file/directory watch resources, bounded event queues,
feature-gated V8/stdlib watch entry points, deterministic cleanup, and honest platform
semantics. It must not add Node `fs.watch` compatibility, recursive watch claims,
diagnostic/doctor goldens, public alpha docs, benchmark claims, or unrelated runtime APIs.
