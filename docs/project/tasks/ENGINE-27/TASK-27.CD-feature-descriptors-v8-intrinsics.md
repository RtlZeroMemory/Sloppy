# TASK ENGINE-27.C/D: Feature Descriptors and V8 Intrinsic Registration

## Issues

- #502 TASK ENGINE-27.C: Provider/Transport/Stdlib Feature Descriptors
- #503 TASK ENGINE-27.D: V8 Intrinsic Registration by Feature
- Contributes to #489 EPIC ENGINE-27: Runtime Feature Modularity

## Scope

Extend the runtime feature registry descriptors and use the active Plan feature set to
gate V8 intrinsic registration.

Included:

- descriptor metadata for current stdlib imports, provider imports, and implemented V8
  intrinsic namespaces;
- `stdlib.config` and `stdlib.data` feature ids for current stdlib module ownership;
- SQLite provider descriptor mapping to `sloppy/providers/sqlite` and
  `__sloppy.data.sqlite`;
- PostgreSQL and SQL Server provider descriptors as unavailable/deferred entries with no
  JavaScript bridge claim;
- app-host V8 creation borrowing the already-validated `SlRuntimeFeatureSet`;
- V8 bridge registration of app/provider intrinsics only when their active features require
  them;
- V8-gated tests for active SQLite registration and inactive SQLite omission.

Non-goals:

- PostgreSQL or SQL Server JavaScript bridge implementation;
- provider expansion or ENGINE-28 provider executor conversion;
- HTTP-26 policy behavior;
- package include-only-used trimming;
- dynamic runtime feature loading;
- package-manager or Node/npm compatibility.
