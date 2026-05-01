# Post-Core MVP Memory/String/Buffer Audit

Status: 2026-05-01 consolidation audit. This is an adoption map and debt record, not a
runtime rewrite.

## Compliant Areas

| Area | Evidence |
| --- | --- |
| Core primitives | `SlStr`, `SlBytes`, `SlArena`, checked math, builders, and intern tables exist under `include/sloppy/*` and `src/core/*`. |
| HTTP current paths | Parser, body buffering, route/response rendering, and transport accumulation use Slop byte/string builders for the current MVP paths. |
| Diagnostics | Text, JSON, source-frame, redaction, and capability/provider hints use bounded builders. |
| Plan/artifacts | Plan parser and artifact loader use arena-owned copies and stable parsed metadata interning for current artifact execution. |
| V8 interop | Provider-neutral V8 string conversion helpers isolate native/V8 conversion under `src/engine/v8/*`. |
| SQLite | Native provider and V8 bridge copy current row/result/parameter text/blob data into owned/arena-managed storage for implemented paths. |

## Violations Fixed In This PR

No production primitive rewrites were made. The consolidation kept code changes to stale
comments and status text because the remaining adoption gaps are provider/boundary work,
not safe drive-by edits.

## Violations Left As Tech Debt

| Area | Finding | Follow-up |
| --- | --- | --- |
| PostgreSQL provider | Local C-string copy helpers and manual loops should move to checked arena copy helpers; integer/float parameter formatting still uses `snprintf`. | Tracker: `docs/tech-debt-tracker.md#postgresql-provider-copy-helpers`. |
| SQL Server provider | Redaction/copy helpers and streamed text appends use local loops/manual append logic where shared builders would fit. | Tracker: `docs/tech-debt-tracker.md#sqlserver-odbc-redaction`. |
| SQLite V8 bridge | JS parameter arrays can reserve a `std::vector` based on arbitrary JS array length before native bind-count rejection. | Tracker: `docs/tech-debt-tracker.md#sqlite-v8-param-preflight`; issue #431. |
| Tests | A PostgreSQL live test uses `strcpy` after a stack length check. | Tracker: `docs/tech-debt-tracker.md#tests-strcpy-boundary`. |

## Allowed Boundary Uses

- `strlen` in `sl_str_from_cstr` and CLI argv/path boundary adapters.
- Builder internals in `src/core/builder.c`.
- `yyjson_doc_free`, `sqlite3_free`, and driver-owned cleanup APIs at their dependency
  boundaries.
- C++ `std::string`/`std::vector` inside `src/engine/v8/*` where the bridge boundary owns
  conversion, with the SQLite parameter-count caveat above.

## Needs Human Review

- Whether provider-specific numeric formatting should become a shared Slop helper before
  PostgreSQL/SQL Server bridge work.
- Whether CLI output builder cleanup should wait for source-input run or happen as a small
  standards PR.
