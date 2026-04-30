# TASK ENGINE-17.B/D: SQLite Capability, Result, And Error Policy

Status: implemented in this PR.

Issues:

- #341 TASK ENGINE-17.B: SQLite Capability-Wired Open/Use
- #343 TASK ENGINE-17.D: SQLite Result Mapping and Error Policy
- #315 EPIC ENGINE-17: SQLite Runtime and Data Access Completion

Scope:

- enforce Plan-backed SQLite database capabilities before V8 bridge open/use provider work;
- preserve requested handle access on SQLite resources and re-check it for every
  exec/query/queryOne/transaction operation;
- require read capability for query/queryOne, write capability for exec/write operations,
  and readwrite capability for readwrite opens;
- reject missing capability metadata, wrong capability kind, insufficient access,
  provider-token mismatch, and provider-kind mismatch before native SQLite work;
- keep JS-visible SQLite resources as opaque slot/generation handles and map stale,
  closed, invalid, and wrong-kind handles through resource diagnostics without native
  pointer exposure;
- document and test native result mapping for null, integer, float, text, blob, empty
  result sets, queryOne found/not-found, column-name ownership, duplicate column names,
  unsupported values, invalid SQL, constraint failures, and parameter redaction.

Non-goals:

- no HTTP backend work;
- no ENGINE-17.E users API proof;
- no ORM, migrations, public prepared statement handles, PostgreSQL bridge, SQL Server
  bridge, package-manager behavior, public alpha docs, or benchmark claims;
- no conversion of the current SQLite V8 bridge to ENGINE-23 `SERIALIZED_BLOCKING`
  provider executor offload in this slice.

Evidence:

- `data.sqlite.provider` covers native result/error mapping and diagnostic redaction.
- `core.capability.registry` covers database capability policy and provider mismatch
  denial behavior.
- `bootstrap.stdlib.data_foundation` covers public SQLite option/access validation and
  mocked bridge forwarding.
- V8-gated `engine.v8.smoke` covers SQLite resource/capability enforcement when the local
  V8 SDK lane is available.
