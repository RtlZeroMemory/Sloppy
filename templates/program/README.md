# Program Mode Template

The smallest public alpha route-free Program Mode starter. Use it for console
tools, local jobs, or scripts; use [`cli`](../cli/README.md) when you want a
practical CLI layout.

Pre-alpha note: APIs and artifact formats may change between alpha revisions.

## Build, run, package

```sh
sloppy run src/main.ts -- --name Ada
sloppy build
sloppy run .sloppy -- --name Ada
sloppy package
sloppy run .sloppy/package -- --name Ada
```

Arguments after `--` are forwarded to `main(args, ctx)`. Expected output for
the default template:

```text
Hello, Ada.
cwd=<your current directory>
environment=Development
```

## Where to edit next

- Edit `src/main.ts`. The entrypoint is
  `export async function main(args, ctx)`; numeric returns set the process
  exit code (`0..255`).
- Import Sloppy stdlib subpaths (`sloppy/fs`, `sloppy/os`, `sloppy/time`,
  `sloppy/codec`, `sloppy/crypto`) for system access.

## Current limitations

- Pre-alpha. APIs and artifact formats may change between alpha revisions.
- No full Node globals, native addons, or raw terminal APIs. Use the Sloppy
  stdlib or supported `node:*` shims.
- Console output is collected during the run and flushed after the entrypoint
  completes; it is not a streaming terminal interface.
