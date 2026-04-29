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

Not implemented yet:

- no filesystem or network capability APIs;
- no runtime permission enforcement;
- no OS sandboxing;
- no user prompts or grant sources;
- no real database provider access checks.

Related internal docs: `docs/modularity.md`, `docs/data-providers.md`,
`docs/diagnostics.md`.
