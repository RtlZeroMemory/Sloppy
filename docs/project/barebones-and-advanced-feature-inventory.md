# Barebones and Advanced Feature Inventory

Status: inventory for MAIN and MAIN.1 planning.

This document lists skeletons, MVP features, advanced alpha-production needs, and duplicate
warnings. It does not implement anything.

| Item | Current evidence | Why incomplete | User-facing impact | Recommended action | Target roadmap | Existing issue / duplicate warning |
| --- | --- | --- | --- | --- | --- | --- |
| Compiler supported syntax matrix | `compiler/src/sloppyc.rs`, `compiler/tests/fixtures/`, `docs/compiler.md` | Supports a tiny literal JS subset only. | Users can only compile constrained examples. | Define supported syntax matrix, diagnostics, and fixtures. | MAIN.1 | EPIC-21 done; do not duplicate compiler MVP. |
| Source-input `sloppy run` handoff | `src/main.c`, `docs/public/cli.md`, `docs/tech-debt-tracker.md` | `sloppy run <source.js>` intentionally fails; no build/cache handoff. | Users must run `sloppyc build` manually before `sloppy run --artifacts`. | Add source/build handoff after MAIN proves artifact path. | MAIN.1 | New issue needed; not EPIC-22 duplicate. |
| App Plan route/module/provider/capability sections | `docs/app-plan.md`, `docs/modules/plan/README.md`, `tests/integration/execution/compiler_mvp/app.plan.json` | Route metadata is interim; native parser owns only minimal handler schema. | Startup validation and tooling cannot fully trust app graph. | Harden Plan schema and compatibility. | MAIN.1 | EPIC-06 done; new hardening only. |
| Generated artifact hashes | `compiler/tests/fixtures/*/expected/app.plan.json`, `docs/compiler.md` | Hashes are placeholders. | Integrity checks cannot detect plan/bundle drift. | Add deterministic hashing and validation. | MAIN.1 | Avoid duplicating artifact emission MVP. |
| Source map placeholder | `compiler/tests/fixtures/*/expected/app.js.map`, `docs/compiler.md` | Source maps are deterministic placeholders, not useful mappings. | Diagnostics cannot point back to source accurately. | Add source-map fidelity and diagnostic mapping. | MAIN.1 | Related to #34 diagnostics hardening. |
| V8 classic bootstrap runtime | `stdlib/sloppy/internal/runtime-classic.js`, `src/engine/v8/engine_v8.cc` | Classic-script runtime works, true ESM bootstrap loading remains deferred. | Public ESM examples are not the runtime execution shape. | Keep MAIN honest; decide whether ESM is required for MAIN.1 public docs. | MAIN.1 / post-alpha | EPIC-24 done for classic runtime; do not duplicate. |
| V8 optional validation | `.github/workflows/ci.yml`, `tools/windows/dev.ps1`, `docs/quality-score.md` | Default gates do not prove V8. | MAIN evidence must report V8 separately. | Add optional/manual V8 evidence checklist; harden CI later. | MAIN then MAIN.1 | EPIC-26 done for default CI. |
| Request context gaps | `include/sloppy/http_context.h`, `docs/public/routing.md` | Route/query/request context exists, but headers, bodies, typed binding, and limits are absent. | Handlers cannot use many common request features. | Harden supported context and unsupported diagnostics. | MAIN.1 | EPIC-23 done; hardening only. |
| Response writer limitations | `include/sloppy/http_response.h`, `docs/public/results.md` | Limited statuses/content types; no custom headers/cookies/files/streaming. | Public `Results.*` surface cannot all execute natively. | Define alpha-supported result subset and harden failures. | MAIN.1 | EPIC-23 done; hardening only. |
| Production HTTP server | `src/main.c`, `docs/modules/http/README.md` | Dev-only tiny server, no production lifecycle. | Not suitable for production deployment claims. | Keep out of MAIN/M1 unless explicitly scoped. | post-alpha-deferred | Avoid accidental scope creep. |
| Resource table / JS-native handles | `docs/memory.md`, `include/sloppy/scope.h`, issue #35 | Only scope skeleton exists; no resource IDs/generation table. | JS-native database bridge cannot be safe. | Implement resource table before bridge. | MAIN.1 | Keep #35, add dependent issue if needed. |
| JS-to-native SQLite bridge | `stdlib/sloppy/data.js`, `src/data/sqlite.c`, `docs/public/data.md` | Native provider exists but JS `data.sqlite.open` fails bridge-unavailable. | Executable SQLite tutorial cannot honestly ship. | Implement SQLite bridge or explicitly defer demo. | MAIN.1 | Do not duplicate EPIC-16 native provider. |
| PostgreSQL provider live testing | `tests/unit/data/test_postgres.c`, `.github/workflows/ci.yml` | Live tests require env var; CI does not supply service by default. | Live provider confidence remains local/optional. | Add opt-in live service strategy and reports. | MAIN.1 | Do not duplicate EPIC-17 native provider. |
| SQL Server provider live testing | `tests/unit/data/test_sqlserver.c`, `.github/workflows/ci.yml` | Live tests require env/driver; non-Windows defaults disable SQL Server. | Public SQL Server claims need caveats. | Add driver/live strategy if alpha supports it. | MAIN.1 / post-alpha | Do not duplicate EPIC-18 native provider. |
| Capability enforcement | `docs/security-permissions.md`, `docs/public/permissions.md`, issues #152-#156 | Metadata exists; enforcement missing. | Security docs cannot claim real permission gates. | Keep as MAIN.1 blocker for public alpha claims. | MAIN.1-hardening | #130/#152-#156 already open. |
| CLI audit/openapi fixture mode | `src/main.c`, `tests/golden/cli/`, `docs/public/cli.md` | Metadata-only; not full app-host/compiler emitted schema. | CLI tools are useful but not final public introspection. | Harden against compiler/app-host metadata. | MAIN.1 | EPIC-19 done; hardening only. |
| Package smoke limitations | `tools/windows/test-package.ps1`, `tools/unix/package.sh` | Windows smoke exists; Linux/macOS package execution not hosted; no signing. | Distribution remains experimental. | Harden package smoke and runtime dependency strategy. | MAIN.1 | EPIC-25 done for local experimental package. |
| Cross-platform CI gaps | `.github/workflows/ci.yml`, `docs/quality-score.md` | No sanitizer/fuzz matrix, V8 SDK cache, live services, package smoke matrix. | Default green does not prove those paths. | Add optional gates with explicit reporting. | MAIN.1 | EPIC-26 done for default CI. |
| Benchmarks as smoke | `benchmarks/README.md`, `tools/windows/bench.ps1` | Smoke/list checks are not performance claims. | Public perf claims would be misleading. | Add methodology and keep comparisons off. | MAIN.1 | EPIC-20 done for harness. |
| Examples that are API-shape only | `examples/hello/`, `examples/data-foundation/`, `examples/sqlite-basic/`, `examples/modules-basic/` | Many examples use bootstrap API shape, not compiler/runtime execution. | Users may assume examples are runnable. | Label clearly; promote only executable examples. | MAIN.1 | EPIC-28 should be re-scoped. |
| Public alpha docs | `README.md`, `docs/public/`, issues #157-#161 | Some docs lag current reality; public launch docs are premature. | Risk of overclaiming readiness. | Put public alpha docs after MAIN.1 hardening or explicit deferrals. | hide/remove/public-docs-off until MAIN.1 | #131/#157-#161 open, re-scope. |
| Security/secrets redaction | `src/data/postgres.c`, `src/data/sqlserver.c`, `docs/security-permissions.md` | Provider redaction exists, broader security model lacks enforcement. | Secret safety is uneven by area. | Harden diagnostics/redaction and denied-access paths. | MAIN.1 | #156 relates to denied diagnostics. |
| Platform scanner fixtures | `tools/windows/check-platform-boundaries.ps1`, `tools/unix/check-platform-boundaries.sh` | Scanners exist; fixture/self-test hardening remains open. | Boundary checks depend on script behavior. | Add fixtures/self-tests. | MAIN.1 | #26 open. |
| Docs link/semantic freshness | `docs/documentation-policy.md`, `docs/tech-debt-tracker.md` | Docs freshness exists mostly by policy/scanners, not semantic link checks. | Stale docs persist after rapid EPIC merges. | Add targeted docs cleanup and maybe later checker. | MAIN.1 / nice later | #22/#23 may be superseded. |

## Advanced Features Needed Before Alpha-Production

- Resource table and JS-native handles.
- Runtime capability enforcement and denied diagnostics.
- Source-input run handoff or explicit public docs that show the two-step artifact workflow.
- Plan compatibility, hashes, route/module/provider/capability validation.
- Diagnostics JSON/source frames and source-map handoff for user code.
- Executable hello docs and either executable SQLite or explicit SQLite deferral.
- End-to-end conformance suite that runs the supported examples through the real toolchain.
- Cross-platform package/CI gate reporting that distinguishes default, V8, live provider,
  and package-smoke evidence.
