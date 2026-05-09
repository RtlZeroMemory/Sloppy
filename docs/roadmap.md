# Roadmap

This roadmap describes direction and evidence boundaries for the current
pre-alpha phase. It does not replace live issue tracking.

## Current Reality

| Area | Current reality |
| --- | --- |
| Core runtime | Plan parsing, startup validation, diagnostics, and lifecycle checks are active contracts in `src/core/*`. |
| Compiler and artifacts | Runtime execution is artifact-first; source input still compiles before run. |
| V8 bridge | V8 is optional and isolated; default lanes do not imply handler execution evidence. |
| Providers | Native SQLite/PostgreSQL/SQL Server boundaries exist; live environment readiness remains lane-specific evidence. |
| Packaging | Package scripts generate dry-run experimental artifacts, not public release signals. |
| Security and permissions | Capability and metadata validation are enforcement points, not OS sandbox guarantees. |

## Deferred By Design

The following remain out of scope unless implementation and evidence lanes
change:

- Node/Bun/Deno and `node_modules` compatibility claims;
- package-manager app dependency support claims;
- production release or hardening claims;
- performance superiority claims.

## Evidence Policy

Roadmap statements must stay evidence-aware:

- default non-V8 lanes and V8-enabled lanes are separate;
- package smoke and runtime correctness are separate;
- live-provider readiness is separate from native/provider-unit behavior;
- skipped or unavailable optional lanes are never pass claims.
