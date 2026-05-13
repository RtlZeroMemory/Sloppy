# ORM Runtime

The public ORM source lives in `stdlib/sloppy/orm.js`. It is a bootstrap stdlib
asset and is also embedded into the classic script runtime because generated
artifacts read `globalThis.__sloppy_runtime` before ESM stdlib imports are
available.

## Runtime-Classic Embedding

`stdlib/sloppy/internal/runtime-classic.js` contains generated copies of:

- `stdlib/sloppy/schema.js`
- `stdlib/sloppy/orm.js`

The sync tool is:

```powershell
node tools/scripts/sync-orm-runtime-classic.mjs
```

The check mode is run by the bootstrap CTest lane:

```powershell
node tools/scripts/sync-orm-runtime-classic.mjs --check
```

After editing `schema.js` or `orm.js`, run the sync command before running
runtime-classic tests. The check fails if the embedded copy is stale.

## Compiler Contract

The compiler recognizes `sloppy/orm` imports and emits runtime destructuring for
`orm`, `table`, `column`, `relation`, `SloppyOrmError`, and
`SloppyOrmConcurrencyError`.

Plan output marks ORM as visible dynamic metadata and includes static table
metadata when the compiler sees the simple `const Model = table("name", { ... })`
shape:

- `features.orm = true`
- `strongPlan.evidence.orm = true`
- `orm.mode = "runtime-dynamic"`
- `orm.extraction.status = "partial"`
- `orm.tables[]` for statically visible table definitions

That Plan shape is deliberately honest: runtime ORM calls work dynamically, but
the compiler does not claim complete static table or relation extraction until
it can prove the whole catalog.
