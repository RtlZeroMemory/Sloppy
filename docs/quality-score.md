# Quality Score

Status key: Green means usable and enforced, Yellow means usable with gaps, Red means not
ready.

| Category | Status | Why | To move to Green |
| --- | --- | --- | --- |
| Architecture docs | Green | Core boundaries and phases are documented. | Keep ADRs/docs updated as choices change. |
| Story-ready roadmap | Green | Epics have goals, non-goals, tests, and acceptance criteria. | Keep implementation issues mapped to epics. |
| C standards | Yellow | Standards exist and scanner begins enforcement. | Add fixtures and tighten allocator rules after allocator modules land. |
| Platform isolation | Yellow | Scanner and docs exist. | Add scanner self-tests and future Unix CI. |
| Build tooling | Yellow | Windows workflow and CI skeleton exist. | Add more script tests and Unix presets later. |
| Testing | Yellow | Smoke, structural checks, and initial core C unit tests including scope lifetime tests exist. | Add richer C test reporting if needed and golden/snapshot harnesses. |
| Testing intent model | Yellow | Strategy now defines tests-as-documented-intent. | Require implementation PRs to tie tests to source docs and add reviewer enforcement. |
| User-facing docs | Yellow | Planned public docs skeleton exists and marks examples as not implemented. | Fill pages as public features land and add example tests. |
| Module docs | Yellow | Module README skeletons exist with required headings. | Keep module docs updated with implemented behavior and strengthen freshness checks. |
| Simplicity / anti-overengineering | Yellow | C standards and review playbook now define anti-overengineering rules; warning scanner is informational. | Enforce through reviews, add examples as patterns appear, and promote reliable checks. |
| Comment quality / code rationale | Yellow | C standards and review playbook now require useful rationale, ownership, lifetime, and invariant comments without AI-noise. | Enforce through reviews, add examples from repeated findings, and consider lightweight warning scans later. |
| Diagnostics | Yellow | Initial severity, code, source span, builder, text renderer, and snapshot fixtures exist. | Add source frames, JSON output, source maps, and redaction policy when their phases start. |
| Developer ergonomics | Yellow | Product API direction is documented. | Validate with compiler/runtime fixtures later. |
| Modularity | Yellow | Module architecture is documented. | Add plan/module extraction tests in later phases. |
| Data providers | Yellow | Provider model is documented. | Add provider tasks/tests when provider phases begin. |
| Compiler plan | Yellow | `sloppyc` shape is documented and placeholder exists. | Add fake emitter/golden tests, then Oxc integration. |
| Runtime execution plan | Yellow | Execution model is documented, minimal Plan v1 structs exist, the parser validates a documented golden fixture matrix into arena-owned native data, V8 SDK detection is opt-in, the engine-neutral `SlEngine` ABI has noop/unsupported coverage, and V8-enabled builds have a classic script/global function smoke path with basic exception diagnostics. | Add file-based loader, runtime compatibility checks, V8-backed handler-ID execution, and handwritten execution tests later. |
| Concurrency/async model | Yellow | Canonical spec and ADR exist; implementation is future. | Add owner-thread checks, promise settlement tests, cancellation, backpressure, and worker scaling design. |
| Agent harness | Green | Guide, harness docs, playbooks, plans, score, and debt tracker exist. | Keep repeated feedback promoted into docs/checks. |
| Mechanical enforcement | Yellow | Platform, C standards, artifact gates, and first scope lifetime tests exist. | Add allocator/resource/V8 checks as implementation grows. |
