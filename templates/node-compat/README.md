# Node Compatibility Starter

A public alpha Program Mode starter that exercises supported Node
compatibility shims. Sloppy is not a full Node runtime; the shim set grows
over time through explicit slices backed by Sloppy Core APIs.

Public alpha, pre-production. APIs and artifact formats may change between
alpha revisions.

## What this template uses

- `node:path`
- `node:events`
- `node:buffer`
- `node:querystring`

It intentionally avoids `node:stream`, `node:http`, `node:net`, `node:tls`,
`node:child_process`, native addons, and implicit globals such as `process`
or `Buffer`.

## Build, run, package

```sh
sloppy build
sloppy deps .sloppy
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```

`sloppy deps` reports which Node compatibility shims the build resolved.

## Where to edit next

- Import another supported shim from
  [Node compatibility](../../docs/reference/node-compatibility.md).
- Replace shimmed code with first-party Sloppy stdlib calls (`sloppy/fs`,
  `sloppy/os`, `sloppy/codec`) when an equivalent exists; the shim list is
  for compatibility, not idiom.

## Current limitations

- Public alpha, pre-production. APIs and artifact formats may change between
  alpha revisions.
- Node compatibility is partial. Unsupported builtins fail clearly at build
  time (`SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN`). Unsupported members inside a
  partial shim fail at runtime.
- No native Node addons or N-API.
- No global Node `process` or `Buffer` identity.
