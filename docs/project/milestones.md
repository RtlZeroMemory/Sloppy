# Milestones

Milestones are coarse roadmap slices. They group EPICs, not individual commits. Scripts use the same data in `tools/github/milestones.json`.

## 0.0 Foundation / Harness

Purpose: Docs, AGENTS/harness, platform boundaries, GitHub ceremony, and no runtime features.

Included EPICs: EPIC-00

## 0.1 Native Core

Purpose: Platform skeleton, core basics, memory foundation, diagnostics foundation, and resource/lifecycle foundation.

Included EPICs: EPIC-01, EPIC-02, EPIC-03, EPIC-04, EPIC-05

## 0.2 Runtime Contract

Purpose: app.plan schema/loader, V8 bridge smoke, and handwritten artifact execution.

Included EPICs: EPIC-06, EPIC-07, EPIC-08

## 0.3 Async Runtime / HTTP

Purpose: Event loop/concurrency foundation, native completion queue, and HTTP/router foundation.

Included EPICs: EPIC-09, EPIC-10

## 0.4 TypeScript App Host

Purpose: Public TS API, app host foundation, Results/routes/groups/validation ergonomics, modules, config, logging, and services.

Included EPICs: EPIC-11, EPIC-12, EPIC-13, EPIC-14

## 0.5 Data and Capabilities

Purpose: Filesystem/capability model, common data API, SQLite provider, and common provider contracts.

Included EPICs: EPIC-15, EPIC-16

## 0.6 External Providers

Purpose: PostgreSQL, SQL Server, and provider diagnostics/doctor integration.

Included EPICs: EPIC-17, EPIC-18

## 0.7 Tooling and Performance

Purpose: routes/doctor/audit/openapi commands, benchmark suite, and performance validation.

Included EPICs: EPIC-19, EPIC-20
