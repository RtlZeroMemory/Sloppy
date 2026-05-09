# Provider Runtime Model

Sloppy separates provider concerns into four layers so each one can be validated
honestly:

1. Plan metadata (`dataProviders` and `capabilities`) in
   `src/core/plan_parse.c` and `src/core/app_host.c`.
2. Native provider implementations in `src/data/*.c`.
3. V8 bridge modules in `src/engine/v8/intrinsics_*.cc`.
4. Live external service availability (environment- and driver-dependent).

`src/data` implements real provider work (open/query/exec/transactions/pools)
with redaction-aware diagnostics and explicit resource lifetime rules.

The runtime still validates provider and capability metadata at startup. Database
capabilities and provider tokens must cross-reference correctly, and malformed
metadata fails closed.

SQL Server is deliberately explicit about build/runtime gating: when ODBC support
is not enabled, provider APIs return `unsupported` diagnostics rather than fake
success.

This model explains why "provider API surface exists" and "provider executed in
a specific live environment" are separate evidence statements.
