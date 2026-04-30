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
- Keep native runtime behavior honest: ENGINE-04 now consumes GET/POST/PUT/PATCH/DELETE
  route metadata, while provider/capability entries remain metadata for compiler output
  until their bridge layers own enforcement.

## Non-Goals

- Runtime Promise settlement implementation was out of scope for ENGINE-02 and is owned by
  later engine runtime work.
- Non-GET HTTP server/runtime implementation was out of scope for ENGINE-02 and is owned by
  ENGINE-04.
- SQLite native bridge execution from compiled handlers is out of scope.
- Package-manager and Node/npm compatibility are out of scope.
- Public alpha docs and benchmark claims are out of scope.

## Evidence

- Compiler golden fixtures cover HTTP methods, async handler metadata,
  provider/capability metadata, source maps, and rejected unsupported shapes.
- Plan golden fixtures cover GET/POST/PUT/PATCH/DELETE route method validation.
- Native app-host and HTTP route-table tests cover metadata acceptance; ENGINE-04 adds
  runtime method dispatch over that metadata.
- Conformance compile/reject tests cover the supported and unsupported ENGINE-02 compiler
  surfaces through the real `sloppyc build` path.
