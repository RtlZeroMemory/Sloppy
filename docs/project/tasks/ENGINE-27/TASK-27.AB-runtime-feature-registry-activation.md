# TASK ENGINE-27.A/B: Runtime Feature Registry and Plan-Driven Activation

## Issues

- #500 TASK ENGINE-27.A: Runtime Feature Registry
- #501 TASK ENGINE-27.B: Plan-Driven Feature Activation
- Contributes to #489 EPIC ENGINE-27: Runtime Feature Modularity

## Scope

Implement the first runtime feature registry and connect Plan metadata to activation
validation before runtime initialization.

Included:

- stable feature ids for core, V8, HTTP, libuv transport, framework/results/schema stdlib,
  SQLite, PostgreSQL, and SQL Server;
- Plan `requiredFeatures[]` parsing and metadata interning;
- activation derived from Plan target, routes, data providers, and explicit required
  features;
- fail-closed diagnostics for unknown, unavailable, and dependency-missing features;
- default-lane tests for active/inactive feature sets and V8-disabled behavior.

Non-goals:

- V8 intrinsic registration gating by active feature;
- package include-only-used trimming;
- PostgreSQL or SQL Server JavaScript bridge implementation;
- dynamic runtime feature loading;
- package-manager or Node/npm compatibility.
