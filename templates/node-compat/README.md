# Node Compatibility Starter

This public alpha, pre-production Program Mode starter demonstrates explicit
supported Node compatibility shims. Sloppy is not full Node. Unsupported
builtins fail clearly, and the shim set grows over time.

This template uses:

- `node:path`
- `node:events`
- `node:buffer`
- `node:querystring`
- `node:assert`
- `node:process`
- `node:stream` basics
- `node:crypto` basics
- `node:module`, `node:string_decoder`, `node:perf_hooks`, and other package
  compatibility helpers are documented in the Node compatibility reference.

It intentionally avoids full `node:http`, `node:net`, `node:tls`,
`node:child_process`, native addons, and process-wide Node identity. Import
compatibility modules explicitly in source. Bundled package programs may receive
temporary `global`, `process`, and `Buffer` compatibility globals while the
program entry runs.

Pure-JavaScript npm packages that fit the documented exports/imports
resolution shapes (see `tests/fixtures/npm-compat/matrix.json`) can be bundled
alongside this template; native addons, dynamic `require` without
`moduleInclude`, and unsupported Node builtins are rejected with explicit
diagnostics.

```sh
sloppy build
sloppy deps .sloppy --explain
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```
