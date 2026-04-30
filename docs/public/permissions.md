# Permissions

Status: Runtime capability registry and database policy checks implemented; filesystem and
network checks are metadata-only skeletons.

Purpose: document future capabilities, permissions, audit output, and denied-operation
diagnostics.

ENGINE-01 target contract:

- SQLite open/use must check a database capability before provider work.
- Denied SQLite access must return a stable diagnostic and must not reach provider
  execution.
- diagnostics should include safe token, access, provider, route, and handler context where
  known.
- filesystem and network capabilities remain skeleton metadata until APIs exist.
- Sloppy does not claim OS sandboxing, prompt-based grants, or Node/Deno permission
  compatibility.

Implemented database capability metadata example:

```ts
const DataModule = Sloppy.module("data")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      path: ":memory:",
      access: "readwrite",
    });
  });
```

Implemented behavior:

- `builder.capabilities.addDatabase(token, options)` declares root app database capability
  metadata.
- `Sloppy.module(name).capabilities(fn)` declares module-attributed capability metadata.
- capability tokens must be non-empty strings.
- duplicate capability tokens fail.
- `app.capabilities.has(token)`, `get(token)`, and `list()` expose frozen debug metadata.
- database capability metadata currently stores `token`, `kind`, `provider`, `access`,
  `module`, and copied options.
- Plan v1 alpha can carry metadata-only `capabilities` entries with `token`, `kind`,
  `access`, and a provider token reference for database capabilities. The native parser
  validates token syntax, supported kinds/access values, duplicate tokens, required
  database providers, and provider references when the section is present.
- The runtime can build an immutable native capability registry from the parsed plan and
  check database, filesystem, and network capability tokens by kind and access mode.
- Database checks support `read`, `write`, and `readwrite`; provider references must match
  when declared.
- Filesystem checks support `read`, `write`, and `readwrite` as skeleton policy checks
  only. No filesystem API is implemented.
- Network checks support `connect`, `listen`, and `connect-listen` as skeleton policy
  checks only. No network client/listener API is implemented.
- SQLite examples include `path: ":memory:"` provider metadata plus a capability token.
  V8-enabled SQLite bridge calls check that token against the runtime registry before
  opening or using SQLite. File database policy beyond that metadata check is still
  deferred.
- PostgreSQL copied metadata must not normalize or store credential-bearing fields such
  as `connectionString`. Use a secret-store reference, config key, or already-redacted
  placeholder before metadata is persisted or displayed. Diagnostics and PR notes must
  also redact credentials; runtime network permission enforcement is still deferred.
- SQL Server follows the same secret rule: database capability metadata should prefer a
  config key such as `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` over storing an ODBC
  connection string. Diagnostics and PR notes must redact `PWD`, `Password`, and access
  token fields. Runtime network permission enforcement is still deferred.

Not implemented yet:

- no filesystem or network APIs;
- no OS sandboxing;
- no user prompts or grant sources;
- no JavaScript PostgreSQL or SQL Server provider access checks because those bridges do
  not exist yet.
- no Node/Deno permission compatibility.

Provider enforcement note: native check hooks deny before provider work when a caller uses
them. The JavaScript SQLite bridge uses those hooks on open/read/write operations in
V8-enabled runtime contexts.

CLI audit note: `sloppy audit` is metadata-only. It can flag missing or mismatched
provider/capability metadata and filesystem/network skeleton capabilities, but it does not
execute handlers, open providers, contact live services, generate auth/security OpenAPI
schemes, or prove OS sandboxing.

Related internal docs: `docs/modularity.md`, `docs/data-providers.md`,
`docs/diagnostics.md`.
