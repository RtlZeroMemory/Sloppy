# TASK ENGINE-27.E/F: Missing Feature Diagnostics and Package Policy

## Issues

- #504 TASK ENGINE-27.E: Missing Feature Diagnostics
- #505 TASK ENGINE-27.F: Package Include-Only-Used Feature Policy
- Contributes to #489 EPIC ENGINE-27: Runtime Feature Modularity

## Scope

Pin deterministic diagnostics for missing, unavailable, disabled, and dependency-missing
runtime features, then define the honest package include-only-used policy.

Included:

- renderer goldens for unknown `requiredFeatures[]`, unavailable PostgreSQL/SQL Server,
  disabled SQLite availability, disabled V8, missing `transport.libuv`, and
  dependency-missing HTTP transport activation;
- stdlib missing-intrinsic snapshot for inactive `provider.sqlite`;
- stdlib PostgreSQL and SQL Server unavailable bridge messages that use stable runtime
  feature ids and keep connection strings redacted;
- package policy that distinguishes compiled/staged inclusion from Plan-driven runtime
  activation;
- documentation that current archives may still stage broad stdlib assets and compiled
  provider code until future trimming work consumes feature descriptors.

Non-goals:

- package-manager behavior or Node/npm compatibility;
- installers, public releases, or public alpha docs;
- PostgreSQL or SQL Server JavaScript bridge implementation;
- provider expansion or ENGINE-28 provider executor conversion;
- HTTP-26 policy behavior;
- metrics, torture/stress harnesses, or benchmark claims;
- dynamic runtime feature loading or plugin ABI.
