# ENGINE-02 Compiler Supported App Pipeline

Status: implemented in PR branch, pending review.

## Goal

Make supported Sloppy apps compile into deterministic runtime artifacts that can drive the
next HTTP, async, SQLite, capability, and diagnostics layers without pretending those
runtime layers already exist.

## Scope

- Extract `app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete`
  route metadata.
- Emit direct async handlers as compiler metadata and generated JavaScript only.
- Emit minimal SQLite `dataProviders` and database `capabilities` plan metadata from
  `builder.capabilities.addDatabase(...)`.
- Emit deterministic `app.plan.json`, `app.js`, and handler-line `app.js.map` artifacts.
- Reject unsupported source shapes before writing success artifacts.
- Keep native runtime behavior honest: current dev route table remains GET-only, Promise
  settlement remains unsupported, and provider/capability entries remain metadata.

## Non-Goals

- No runtime Promise settlement implementation.
- No non-GET HTTP server/runtime implementation.
- No SQLite native bridge implementation from compiled handlers.
- No package-manager or Node/npm compatibility.
- No public alpha docs or benchmark claims.

## Evidence

- Compiler golden fixtures cover HTTP methods, async handler metadata,
  provider/capability metadata, source maps, and rejected unsupported shapes.
- Plan golden fixtures cover GET/POST/PUT/PATCH/DELETE route method validation.
- Native app-host and HTTP route-table tests cover metadata acceptance while keeping the
  dev runtime GET-only.
- Conformance compile/reject tests cover the supported and unsupported ENGINE-02 compiler
  surfaces through the real `sloppyc build` path.
