# Runtime Diagnostics

Sloppy runtime diagnostics have two layers:

- `SlDiag` remains the small, arena-owned diagnostic value used by native code.
- Diagnostic reports wrap a `SlDiag` with stable taxonomy metadata and optional runtime context.

Reports are local artifacts only. Sloppy does not upload telemetry or contact a reporting service.

## Taxonomy

Each report includes `schemaVersion`, `kind`, `code`, `severity`, `subsystem`, `phase`,
`status`, `safeToExpose`, `redaction`, optional runtime context, and optional cause text.

Provider and native invariant reports default to strict or non-exposable metadata. Report
rendering redacts common secret-bearing strings in diagnostic messages and cause text.

## Breadcrumbs

The runtime owns a fixed-size breadcrumb ring. It records bounded failure and
diagnostic events such as Plan load, artifact validation, HTTP request dispatch
failures, V8 handler failures, stream failures, provider worker failures,
package report writes, doctor report writes, and fatal native invariants.
Routine HTTP/V8 success breadcrumbs on the request hot path are disabled by
default. Set `SLOPPY_SUCCESS_BREADCRUMBS=1` when a local diagnostic capture
needs successful request start, route match, native response hit, or handler
entry/exit breadcrumbs.

Breadcrumb recording does not allocate. Rendering `breadcrumbs.jsonl` is
explicit and local.

## Crash Reports

Native invariant failures write a local report through the crash-report writer before aborting.
The report writer can also be called directly by tools that need a local failure artifact.
Crash reports are written to a unique run directory under
`.sloppy/reports/crashes/<counter>-<pid>/` by default. Each directory contains `crash.json`
and, when breadcrumbs are enabled, `breadcrumbs.jsonl`.

Current limitations:

- crash reports are local JSON files under `.sloppy/reports/crashes` by default;
- there is no telemetry transport;
- low-level fatal signal integration is not a production crash dumper;
- reports contain stable runtime context only when the caller supplies it.
