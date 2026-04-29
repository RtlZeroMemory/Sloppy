# Permissions

Status: Bootstrap database capability metadata skeleton implemented.

Purpose: document future capabilities, permissions, audit output, and denied-operation
diagnostics.

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
- SQLite examples may include `path: ":memory:"` in copied metadata. This is provider
  metadata only today; runtime permission enforcement and file database policy are still
  deferred.
- PostgreSQL copied metadata must not normalize or store credential-bearing fields such
  as `connectionString`. Use a secret-store reference, config key, or already-redacted
  placeholder before metadata is persisted or displayed. Diagnostics and PR notes must
  also redact credentials; runtime network permission enforcement is still deferred.

Not implemented yet:

- no filesystem or network capability APIs;
- no runtime permission enforcement;
- no OS sandboxing;
- no user prompts or grant sources;
- no JavaScript database provider access checks.

Related internal docs: `docs/modularity.md`, `docs/data-providers.md`,
`docs/diagnostics.md`.
