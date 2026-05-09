# Plan

`app.plan.json` is the contract between the compiler and the runtime. It
is JSON, deterministic, and the only file the runtime reads to decide
what your app *is* before evaluating any JavaScript.

The user-facing schema reference lives at
[reference/plan-format.md](../reference/plan-format.md). This page covers
internals — how the runtime parses and validates it.

## Where it's parsed

| Step                                | File                       |
| ----------------------------------- | -------------------------- |
| Read JSON, allocate parsed structure | `src/core/plan_parse.c`   |
| Validate field shapes and relationships | `src/core/plan_parse.c` |
| Cross-check vs runtime capabilities | `src/core/app_host.c`      |
| Public types                        | `include/sloppy/plan.h`    |

`SlPlan` is arena-owned (see [memory-model.md](memory-model.md)). A
parsed Plan is a single allocation tree freed when the app exits or when
parsing fails.

## Validation order

Order matters: each step assumes the previous one succeeded.

```text
1. yyjson parse                     malformed JSON → reject
2. schemaVersion check              unknown version → reject
3. compiler/runtime version check   minimum mismatch → reject
4. target check                     platform/engine wrong → reject
5. artifact list                    missing file or hash → reject
6. handler table                    duplicate IDs → reject
7. route table                      duplicate (method,pattern) → reject
                                    duplicate non-empty names → reject
                                    unknown handlerId → reject
8. providers                        duplicate tokens, bad shape → reject
9. capabilities                     duplicate tokens, bad shape → reject
10. requiredFeatures                unknown feature → reject
11. server config                   bad host/port/limits → reject
12. config requirements             malformed → reject
13. secret redaction sweep          embedded credential → reject
```

Every rejection is a structured `SlDiag` with a stable code and a
source location pointing at the offending JSON path.

## What gets stored

`SlPlan` carries arena-owned views of:

- `schema_version`, `compiler_version`, `runtime_min_version`, `target`
- `artifacts[]` — name + 32-byte SHA-256 hash + size
- `handlers[]` — id (numeric), kind, source span
- `routes[]` — method, pattern (parsed once), handlerId, name, tags,
  bindings (Plan v2 framework metadata where present), source span
- `providers[]` — name, kind, runtime config metadata (no secrets)
- `capabilities[]` — token, kind, provider, access, metadata
- `required_features[]`
- `server` — host, port, max connections, body limits, timeouts, TLS metadata
- `config` — environment-resolved keys the compiler saw

Strings and arrays are interned where it matters
(`src/core/intern.c`) — repeated route patterns, handler IDs, and
capability tokens share storage.

## Schema versioning

`schemaVersion` is a single string today (`"plan/v1-alpha"`). Pre-alpha
breaking changes are expected. The parser rejects any version it doesn't
recognize.

When the schema bumps, both ends move together: `sloppyc` writes the new
version, the runtime parser learns it, the test fixtures regenerate.
There is no on-the-fly upgrade path — old artifacts are recompiled.

## Artifact hashing

Each entry in `artifacts[]` records:

```jsonc
{ "name": "app.js", "size": 12345, "hash": "sha256:abcd…" }
```

The runtime reads `app.js` (and `app.js.map` if present), computes the
SHA-256, and refuses to evaluate the bundle if it doesn't match. This
catches partial copies, corrupted artifacts, and accidental edits.

## Route table construction

Plan-validated routes feed into the native route table. The order of
operations:

1. Parse each `pattern` once with `sl_route_pattern_parse`
   (`src/core/route.c`).
2. Bind each entry to its `handlerId` and source order.
3. Sort: literal patterns before parameter patterns; ties broken by
   source order.
4. Allocate the dispatch table inside the app arena.

The table is read-only at request time. Lookup is `O(n)` over the table
(small constant — apps have tens of routes, not thousands).

## Secret-redaction sweep

Step 13 walks every string field in the Plan and rejects values that
look like credential strings (anything matching the configured
provider connection-string patterns, anything explicitly tagged secret).
The compiler is expected to never emit credentials into the Plan in the
first place; this sweep is a belt-and-braces check.

If your app's source somehow ends up with a literal password, you'll
get a Plan rejection at startup with a diagnostic pointing at the field.

## Tests

- **Goldens** under `tests/golden/plan/**` pin expected Plan content for
  representative apps. Compiler changes that alter Plan shape have to
  update goldens explicitly.
- **Plan parser unit tests** under `tests/unit/core/test_plan*.c` cover
  every rejection branch.
- **End-to-end** runs `tests/cmake/check_source_input_run.cmake` builds
  source through the CLI and verifies the full Plan/run path.

## See also

- [reference/plan-format.md](../reference/plan-format.md) — full field reference
- [Compiler](compiler.md) — how the Plan is produced
- [Runtime](runtime.md) — what happens after validation
