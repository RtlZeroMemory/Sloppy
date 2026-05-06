# CORE-CONFIG-01 Issue Index

Parent: #693 CORE-CONFIG-01.

- #694 API contract, source precedence, key model.
- #695 config file, environment, command-line, and override providers.
- #696 typed reads, binding, defaults, and validation contracts.
- #697 secrets, redaction, user secrets, and safe diagnostics.
- #698 provider-owned configuration contracts.
- #699 compiler, Plan, doctor, and package metadata integration.
- #700 strict mode, dynamic keys, raw Environment access policy.
- #701 runtime loader, reload/dev behavior, and app lifecycle.
- #702 tests, fixtures, goldens, fuzz, and negative paths.
- #703 examples, internal docs, and core platform map integration.

Deferred child-scope notes:

- Reload-on-change watchers are not implemented.
- Production secret vaults are not implemented.
- PostgreSQL and SQL Server JavaScript provider bridges are not implemented by this config
  slice.
- Dynamic config keys require explicit future declarations before strict/package tooling can
  depend on them.
