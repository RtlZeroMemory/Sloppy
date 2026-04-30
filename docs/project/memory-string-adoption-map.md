# Memory/String Adoption Map

Status: strategic design for ENGINE-22 after the ENGINE-21.A/B/C/D/E/F primitive slice,
with ENGINE-22.A HTTP adoption implemented as the first subsystem pass.

ENGINE-22 is the migration layer after ENGINE-21 primitives land. ENGINE-21.A/B/C/D/E/F now
provide view/copy/hash helpers, bounded builders, bounded interning, focused safety tests,
private V8 string interop helpers, and SQLite text/blob copy helpers. ENGINE-22 should not
be started by inventing helper code in individual subsystems; start with the primitive
contracts, then move hot paths in bounded PRs.

Hot paths are marked with `hot`.

| Subsystem | Current files | Current primitive/pattern | Problem/risk | Target primitive | Expected benefit | Migration risk | Suggested task | Must be done before |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| HTTP parser/request/response `hot` | `src/core/http.c`, `src/core/http_context.c`, `src/core/http_response.c`, `src/core/route.c`, `src/main.c` | ENGINE-22.A now uses `SlStringBuilder` for segmented request target/header accumulation, `SlByteBuilder` for bounded body accumulation and response output, `sl_str_copy_to_arena` for stable request/route-owned strings, and exact `SlBytes` output for responses. CLI socket buffers remain dev-path fixed buffers. | Complete-buffer parsing is still the current dev runtime shape; streaming/backend ownership, keep-alive, and socket lifecycle are ENGINE-13 work. | Request-owned buffer policy, builder-backed parser/response output, header/body lifetime rules, and later backend-owned response target. | Lower duplicate append logic, clearer request/body/response lifetime, non-NUL/binary edge coverage. | Medium/high: behavior is preserved, but future backend adoption must not race this writer ownership. | ENGINE-22.A implemented; backend continuation belongs to ENGINE-13 and ENGINE-22.F cleanup. | ENGINE-13 proper HTTP backend and ENGINE-19 HTTP conformance. |
| V8 bridge string conversions `hot` | `src/engine/v8/engine_v8.cc`, `src/engine/v8/http_bridge.cc`, `src/engine/v8/intrinsics_sqlite.cc`, `src/engine/v8/string_interop.*` | Private V8 string interop helpers now own native view to V8 strings, V8 value to `std::string` staging, and arena-owned native text/byte copies. Some call sites still use local staging before full adoption. | ENGINE-22.D still needs to finish replacing remaining ad hoc staging and make conversion diagnostics consistent across bridge modules. | Central V8 string conversion helpers and arena/request-owned native strings everywhere. | Predictable native/V8 lifetime, fewer duplicated helpers, safer async evolution. | High: V8-gated behavior and owner-thread policy. | ENGINE-22.D | ENGINE-14 module/bootstrap, ENGINE-15 diagnostics, ENGINE-17 SQLite bridge. |
| SQLite row/result conversion `hot` | `src/data/sqlite.c`, `include/sloppy/data_sqlite.h`, `src/engine/v8/intrinsics_sqlite.cc` | Arena-owned text/blob result cells, SQLite transient text/blob copy helpers, parameter text/blob copy helpers for future operation-owned submission, and JS bridge blob materialization as V8-owned bytes. | ENGINE-22.E still needs broader adoption for operation-owned parameters, prepared/async policy, row-shape decisions, and request/app ownership integration. | SQLite text/blob ownership policy, owned parameter buffers, V8 conversion helpers. | Safe provider offload path, clear result lifetime, stable row mapping. | High: provider and V8 bridge interaction. | ENGINE-22.E | ENGINE-17 SQLite runtime completion and ENGINE-19 SQLite conformance. |
| Diagnostics/source frames/JSON `hot on failures` | `src/core/diagnostics.c`, `src/core/capability.c`, `src/core/http.c`, `src/core/route.c`, `src/core/plan_parse.c` | ENGINE-22.B adopted shared bounded string builders for diagnostic text, JSON diagnostics, and source frames; subsystem-local hint construction still exists outside touched paths. | Future source-map diagnostics can still expand subsystem-local formatting if new paths bypass the renderer. | String builder, formatter, redaction builder. | Stable output with less repeated formatting code. | Medium/high: golden snapshot drift risk. | ENGINE-22.B | ENGINE-15 diagnostic completion. |
| Plan parser/artifact loader | `src/core/plan_parse.c`, `include/sloppy/plan.h`, `src/main.c` | yyjson strings copied into arena; CLI loads fixed buffers and joins paths manually. | Plan parser is good; artifact loader/path/hash strings are ad hoc and CLI-local. | Arena copy helpers, path/string builder policy for artifact paths, stable byte spans for loaded assets. | Preserve parser safety while reducing CLI duplication. | Medium: startup failures and artifact diagnostics are sensitive. | ENGINE-22.C | ENGINE-14 bootstrap loading, ENGINE-18 CLI/dev loop, ENGINE-20 strong Plan. |
| Plan/route/module/provider symbols `hot at startup and lookup` | `src/core/plan_parse.c`, `src/core/route.c`, `src/core/app_host.c`, `src/core/capability.c`, `src/core/runtime_registry.c` | Repeated app-lifetime strings are copied/views and compared by bytes; no stable interned symbol abstraction exists. | Route/Plan/provider graphs can accumulate duplicate names and repeated byte comparisons; later typed Plan work needs stable metadata identity. | Bounded app/static intern table, symbol IDs or interned views, collision-safe hash/equality helpers. | Lower duplicate metadata ownership, faster stable graph lookup, clearer app lifetime. | High: public behavior must remain byte-equality correct and secrets must never be interned. | ENGINE-21.F then ENGINE-22.C/F | ENGINE-20 strong Plan, ENGINE-14 module/bootstrap, ENGINE-16 lifecycle. |
| CLI output | `src/main.c` | `SlCliSpan`, direct `printf`/`fwrite`, local JSON escaping, shared diagnostic redaction for doctor messages, and diagnostic text rendering for `sloppy run` startup diagnostics. | Some command output still streams directly and CLI-wide diagnostic-format selection remains deferred. | Shared string builder/diagnostic JSON builder and redaction formatter. | Consistent text/JSON output and fewer CLI-only helpers. | Medium: process golden output may change. | ENGINE-22.B | ENGINE-18 CLI/dev loop and ENGINE-15 diagnostics. |
| App host/runtime lifecycle | `src/core/app_host.c`, `src/core/scope.c`, `src/core/resource.c`, `include/sloppy/app_host.h` | Request cleanup scope and resource table; app/request/temp arena family not complete. | Request/app memory ownership for async/lifecycle work is still not explicit enough. | Lifetime model, app/request/scratch arena conventions, leak-oriented tests. | Cleanup and async retention become predictable. | High: lifecycle boundaries affect HTTP, V8, SQLite. | ENGINE-21.A then ENGINE-22.F | ENGINE-16 lifecycle runtime and ENGINE-12 async backend. |
| Compiler artifacts/source maps | `compiler/src/sloppyc.rs`, `compiler/tests/*` | Rust `String`/`Vec`, deterministic serde output, source-map string generation. | Mostly acceptable; source-map and generated artifact strings must stay deterministic and path-normalized. | Deterministic artifact/string policy, no C primitive migration needed. | Prevents accidental nondeterminism and source path leakage. | Medium: golden fixture churn. | ENGINE-22.C | ENGINE-15 source maps and ENGINE-20 Plan layer. |
| Conformance/bench smoke | `tests/conformance/*`, `benchmarks/*`, `docs/testing*.md` | Conformance reports default vs V8-gated; benchmarks are harness/list/smoke and selected microbenchmarks. | Allocation behavior is not systematically guarded; benchmark smoke must not become performance claim. | Allocation-aware tests where possible, sanitizer/fuzz hooks, benchmark smoke labeling. | Regression visibility without marketing claims. | Medium: avoid noisy gates. | ENGINE-21.E and ENGINE-22.F | ENGINE-19 conformance compatibility. |

## Recommended Implementation Order

1. ENGINE-21.D locks V8/SQLite conversion policy on top of the implemented primitive
   layer. Done; continue with adoption rather than adding new subsystem-local helpers.
2. ENGINE-22.A migrates HTTP parser/request/response paths. Done for the current
   complete-buffer dev runtime; ENGINE-13 owns backend/socket lifecycle continuation.
3. ENGINE-22.B migrates diagnostics and CLI output.
4. ENGINE-22.C migrates Plan/artifact/source-map loader patterns and starts interned
   metadata adoption where byte-equality behavior stays unchanged.
5. ENGINE-22.D migrates V8 bridge conversions after #367 policy is accepted.
6. ENGINE-22.E migrates SQLite result/parameter conversion after #367 policy is accepted.
7. ENGINE-22.F removes duplicate ad hoc buffers/builders, finishes safe symbol adoption,
   and adds regression guards.

## Parallelization Notes

Can run in parallel:

- ENGINE-21.A/B docs/tests and ENGINE-21.C builder design, as long as names and ownership
  contracts are reconciled before implementation lands.
- ENGINE-21.F intern-table design can proceed after ENGINE-21.B hash/equality decisions are
  stable, but its implementation should not race route/Plan adoption.
- ENGINE-22.B diagnostics/CLI planning and ENGINE-22.C Plan/artifact planning after
  ENGINE-21 builder contracts are stable.
- Conformance/benchmark guard design while runtime adoption PRs are in progress.

Should not run in parallel:

- V8 conversion adoption and SQLite bridge adoption if both edit
  `src/engine/v8/intrinsics_sqlite.cc`.
- HTTP response builder adoption and proper HTTP backend response/body implementation if
  both change writer ownership.
- Diagnostics builder adoption and diagnostic golden expansion without one owner for
  expected output.
- Interned-symbol adoption in Plan/route/capability graphs and typed Plan graph changes if
  both alter startup validation or identity semantics.
- Request lifetime arena changes and async/backend cleanup changes unless ENGINE-16/12
  ownership is locked.
