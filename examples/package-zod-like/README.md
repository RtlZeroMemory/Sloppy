# Package: zod-like

This example uses a local fixture package to demonstrate Sloppy's installed
package graph. It does not require internet access.

The fixture package is intentionally small and zod-shaped: it exports a
validator factory with `string()`, `number()`, and `object(...)` helpers. It is
not the real Zod package.

## Setup

```sh
npm install
sloppy build
sloppy deps .sloppy
sloppy run -- Ada
```

`npm install` installs `fixtures/zod-like` through a local `file:` dependency.
Real installed packages work when their JavaScript and runtime API usage are
compatible with Sloppy's loader and Node compatibility shims.

## What It Covers

- installed package resolution from `node_modules`;
- package.json `exports`;
- ESM package import from Program Mode;
- dependency graph inspection with `sloppy deps`.
