# Memory/String Current-State Audit

Status: strategic audit for ENGINE-21 and ENGINE-22.

This audit is evidence for the memory/string foundation roadmap. It does not implement
new primitives or migrate call sites. The guiding rule is:

> Include only what is defined. Configure only what is configured. Allocate only where
> ownership requires it.

## Summary

Slop already has a solid first layer: borrowed `SlStr` and `SlBytes`, checked size math,
caller-backed `SlArena`, arena rollback patterns, deterministic diagnostics, scoped
cleanup, generation-counted resource IDs, and arena-owned parser/provider output in several
subsystems.

It does not yet have the complete memory/string building-block layer needed for the engine
foundation. Missing pieces include owned string/buffer conventions, `SlBuf`, string/byte
builders, formatter helpers, a bounded string interning/symbol-table primitive,
centralized V8/native conversion policy, centralized SQLite text/blob ownership policy,
stronger raw-allocation enforcement, fuzz/sanitizer hooks, and a deliberate adoption plan
for hot paths.

The old open task `TASK 03.B: String Builder / Buffer Foundation` remains valid but too
narrow for the current engine roadmap. ENGINE-21 should absorb that unfinished primitive
work, and ENGINE-22 should track adoption/refactor work across subsystems.

## Existing Primitives

| Primitive or pattern | Evidence | Current behavior | Audit classification |
| --- | --- | --- | --- |
| Borrowed string view | `include/sloppy/string.h`, `src/core/string.c`, `tests/unit/core/test_string.c` | `SlStr` is pointer plus length, not owning and not necessarily NUL-terminated. `sl_str_from_cstr` is the C-string boundary adapter. | Safe/acceptable foundation, but helper surface is tiny. |
| Borrowed byte view | `include/sloppy/bytes.h`, `src/core/bytes.c`, `tests/unit/core/test_bytes.c` | `SlBytes` is pointer plus length for non-owning bytes. | Safe/acceptable foundation, but no owned byte buffer exists. |
| Checked size math | `include/sloppy/checked_math.h`, `src/core/checked_math.c` | Allocation-sized add/multiply return `SlStatus` and preserve output on failure. | Safe/acceptable. |
| Caller-backed arena | `include/sloppy/arena.h`, `src/core/arena.c`, `tests/unit/core/test_arena.c` | `SlArena` never owns backing storage; it supports aligned allocation, marks, reset, stale-mark generation checks, high-water stats, and debug poisoning. | Safe/acceptable foundation, but no allocator/scratch/request arena family exists yet. |
| Cleanup scope | `include/sloppy/scope.h`, `src/core/scope.c` | `SlScope` owns cleanup registrations only, with caller or arena-backed storage. | Safe/acceptable, needs adoption with request/app lifetime. |
| Resource table | `include/sloppy/resource.h`, `src/core/resource.c` | Generation-counted IDs avoid raw JS-visible native pointers. Storage is caller/arena backed. | Safe/acceptable for handles; not a general memory primitive. |
| Diagnostic builder/renderers | `include/sloppy/diagnostics.h`, `src/core/diagnostics.c` | Builder copies text into arena; renderers compute length, allocate one arena buffer, and write text/JSON/source frames. | Safe but duplicated writer logic; target for shared string builder/formatter. |
| HTTP request-head storage | `include/sloppy/http.h`, `src/core/http.c` | llhttp callbacks copy raw target, path, header names, and values into caller arena; parser resets to mark on failure. | Safe/acceptable for complete-buffer skeleton; header fragment accumulation is a builder-like pattern. |
| HTTP response writer | `include/sloppy/http_response.h`, `src/core/http_response.c` | Writes status line, fixed headers, content length, and body into caller-provided buffer byte by byte. | Bounded and safe, but ad hoc byte builder. |
| SQLite native row/result ownership | `include/sloppy/data_sqlite.h`, `src/data/sqlite.c` | Column names and text values are copied into caller arena; SQLite statement handles are finalized on every path. | Safe/acceptable, but blob/owned-buffer policy and JS conversion strategy are incomplete. |
| V8 string conversions | `src/engine/v8/engine_v8.cc`, `src/engine/v8/intrinsics_sqlite.cc` | Bridge converts V8 strings through `std::string`/`v8::String::Utf8Value`, then copies supported results into arenas. | Acceptable inside V8 boundary, but duplicated conversion policy and hot-path allocation risk. |
| Plan parser string ownership | `include/sloppy/plan.h`, `src/core/plan_parse.c` | yyjson strings are copied into caller arena; parser rolls back arena on failure and builds diagnostics after rollback. | Safe/acceptable. |
| String interning/symbol tables | Search for intern/symbol-table primitives under `include/sloppy/*` and `src/*` | No app-lifetime intern table exists for repeated route, Plan, module, capability, provider, diagnostic-code, or HTTP-token strings. | Missing foundation for stable metadata identity and low-copy app graphs. |
| CLI spans and buffers | `src/main.c` | CLI uses local `SlCliSpan`, fixed buffers, direct stdout/stderr writing, and ad hoc JSON/redaction/path helpers. | Acceptable for CLI MVP, but duplicated string/builder logic. |
| Compiler strings | `compiler/src/sloppyc.rs` | Rust compiler owns source/artifact strings with `String`/`Vec` and serde output. | Acceptable for Rust compiler layer; adoption focus is deterministic output, not C primitives. |

## Current Allocation Patterns

| Subsystem | Current pattern | Classification | Notes |
| --- | --- | --- | --- |
| App host/runtime | Native app startup validation uses borrowed plan data and request cleanup scopes. Request/app arenas are not yet a full lifecycle family. | Safe/acceptable plus future optimization target. | ENGINE-16 and ENGINE-22 should align request/app lifetime with memory primitives. |
| HTTP | Request-head parser copies into arena; route matches allocate captures from match arena; response writer uses caller buffer; dev server uses fixed buffers in CLI. | Safe/acceptable, duplicated primitive, hot-path allocation risk. | Request parse, route matching, body read, and response write are hot paths. |
| V8 bridge | V8 boundary uses `std::string`, `std::vector`, `Utf8Value`, and arena copies. | Safe inside boundary, duplicated primitive, hot-path allocation risk. | Native/V8 string conversions need one policy so request context, results, exceptions, and SQLite bridge do not diverge. |
| SQLite/data | Native providers use provider APIs, caller-owned connection structs, arena-owned result materialization, and provider-specific redaction. | Safe/acceptable plus unclear future blob/result lifetime. | SQLite row mapping and JS object conversion are hot paths. |
| Diagnostics | Diagnostic renderers use length calculators plus custom writers. Capability/route/HTTP helpers build hints with local arena buffers. | Duplicated primitive, future optimization target. | ENGINE-15 should consume a shared builder once ENGINE-21 lands. |
| Plan parser/artifact loader | Plan parser copies yyjson strings into arena with rollback. CLI artifact loader uses fixed file buffers and path buffers. | Parser safe/acceptable; loader has ad hoc buffers. | Artifact path/hash/source-map strings belong in ENGINE-22.C adoption. |
| Plan/route/module metadata | Strings are copied into arenas and compared by byte equality; no symbol table exists. | Safe/acceptable now, future optimization target. | Repeated app-lifetime metadata should adopt a bounded intern table where it improves lookup and ownership clarity. |
| CLI | Metadata commands use `SlCliSpan`, fixed buffers, direct format code, and yyjson-owned spans while docs stay alive. | Duplicated primitive, unclear shared formatting. | CLI diagnostics/output should adopt shared builders after primitive layer. |
| Compiler | Rust uses owned `String`, `Vec`, `serde_json`, `format!`, and source slices. | Safe/acceptable for compiler layer. | Future source-map/artifact output should keep deterministic string construction. |

## Evidence Findings

| Finding | Evidence | Current behavior | Risk | Recommended target primitive |
| --- | --- | --- | --- | --- |
| `SlStr` and `SlBytes` are borrowed only. | `include/sloppy/string.h`, `include/sloppy/bytes.h` | Views do not allocate, own, validate UTF-8, or guarantee NUL termination. | Correct but incomplete for ownership-heavy boundaries. | ENGINE-21.B owned string/byte view helpers and explicit copy helpers. |
| Arena is caller-backed only. | `include/sloppy/arena.h`, `src/core/arena.c` | Arena does not own backing storage and is not a general allocator. | Good safety boundary, but no app/request/scratch arena policy. | ENGINE-21.A lifetime/allocation model. |
| String builder/buffer foundation is not implemented. | `docs/memory.md`, `docs/modules/memory/README.md`, issue #32 | `SlBuf` and `SlStringBuilder` are documented as future work. | Ad hoc writers will continue to multiply. | ENGINE-21.C string/byte builder and formatter utilities. |
| String interning is not implemented. | Search for intern/symbol-table primitives; `include/sloppy/*`, `src/core/*`, `src/engine/v8/*`, `src/data/*` | Repeated metadata strings remain ordinary copies/views, and route/Plan/provider names have no stable symbol abstraction. | Future app graphs may accumulate repeated app-lifetime copies and repeated byte comparisons. | ENGINE-21.F bounded intern table and symbol policy; ENGINE-22.C/F adoption. |
| HTTP parser accumulates fragmented llhttp strings manually. | `src/core/http.c` `sl_http_append_str`, `sl_http_on_url`, `sl_http_on_header_field`, `sl_http_on_header_value` | Each fragment append creates a new arena allocation and copies current plus chunk. | Hot-path allocation/copy risk for split headers or long targets. | ENGINE-22.A adopt byte/string builder or parser-specific accumulator. |
| HTTP response writer is a hand-written byte builder. | `src/core/http_response.c` `sl_http_response_append_*` | Writes one byte at a time into caller buffer. | Correct but duplicated; future headers/body policy will need a reusable builder target. | ENGINE-21.C and ENGINE-22.A. |
| Diagnostics contain multiple private formatters. | `src/core/diagnostics.c` `sl_diag_write_*`, `sl_diag_json_write_*`, render length functions | Text, source-frame, JSON, and redaction paths each manage length and write offsets. | Duplicated formatting, higher chance of drift. | ENGINE-21.C formatter and ENGINE-22.B diagnostics adoption. |
| Capability hints build strings manually. | `src/core/capability.c` `sl_capability_add_hint_pair` | Allocates a combined hint from arena and copies prefix/value manually. | Small but duplicated pattern. | ENGINE-22.B adoption after builder exists. |
| Plan parser does arena ownership well. | `src/core/plan_parse.c` `sl_plan_parse_json`, `sl_plan_parse_require_string` | yyjson strings are copied to arena; failure resets to saved mark. | Low risk; keep as exemplar. | ENGINE-22.C should preserve this model while adopting shared helpers. |
| CLI has its own span/string layer. | `src/main.c` `SlCliSpan`, `sl_cli_json_escape`, `sl_cli_redact_to_buffer`, `sl_run_join_path` | CLI uses fixed buffers and custom escaping/redaction/path join code. | Duplicated primitive and possible output drift from core diagnostics. | ENGINE-22.B CLI builder adoption. |
| V8 uses `std::string` as conversion staging. | `src/engine/v8/engine_v8.cc` `sl_v8_value_to_string`, `sl_v8_copy_string`, `sl_v8_to_local_string` | Native/V8 strings are converted through V8 APIs and copied into arenas for C results. | Acceptable in bridge, but policy is scattered. | ENGINE-21.D conversion policy and ENGINE-22.D adoption. |
| SQLite bridge uses `std::vector<std::string>` for parameter storage. | `src/engine/v8/intrinsics_sqlite.cc` `sqlite_v8_convert_params` | Text parameters live in vector storage while native call runs. | Correct for sync calls, but not reusable for async/provider offload. | ENGINE-21.D lifetime policy, ENGINE-22.E adoption. |
| Native SQLite copies result text into arena. | `src/data/sqlite.c` `sl_sqlite_read_value`, `sl_sqlite_copy_columns` | Column names and text values are arena-owned after stepping. | Safe; blob policy remains absent. | ENGINE-21.D SQLite text/blob policy, ENGINE-22.E row adoption. |
| PostgreSQL/SQL Server formatting has provider-local helpers. | `src/data/postgres.c`, `src/data/sqlserver.c` | Providers use local numeric formatting, redaction, and string concatenation helpers. | Acceptable but not unified; provider expansion could duplicate. | ENGINE-22.F cleanup after SQLite/core adoption. |
| Benchmarks already avoid measuring some view setup. | `benchmarks/bench_route_matcher.c`, memory notes from prior review | Route benchmark precomputes `SlStr`/`SlBytes` outside timed loop where practical. | Good benchmark hygiene; allocation-aware guards remain basic. | ENGINE-21.E and ENGINE-22.F smoke/benchmark guards. |

## Hot Paths To Protect

- HTTP request parse: target/header accumulation, header lookup, body buffer policy.
- Route matching: path segment views and capture allocation.
- Body read: future buffer ownership, limits, cancellation, and backpressure.
- Response write: header/body serialization and output buffer growth.
- V8 call boundary: native-to-V8 context strings, V8-to-native results, exceptions.
- SQLite row mapping: provider text/blob copy, row object materialization, parameter binding.
- Plan/route graph startup: stable metadata names, route names, module names, capability
  names, provider names, and repeated diagnostic identifiers.
- Diagnostics on failure: source frames, JSON diagnostics, redaction, CLI output.

## Recommended Roadmap Outcome

- ENGINE-21 should implement and test the missing primitive contracts, including a bounded
  app/static-lifetime interning/symbol-table primitive for stable metadata strings.
- ENGINE-22 should migrate hot paths and remove duplicated ad hoc builders after ENGINE-21
  lands.
- Existing issue #32 should be treated as superseded or absorbed by ENGINE-21.C rather than
  left as the only string/buffer tracker.
