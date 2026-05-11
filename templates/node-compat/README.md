# Node Compatibility Starter

This public alpha Program Mode starter demonstrates explicit supported Node
compatibility shims. Sloppy is not full Node. Unsupported builtins fail
clearly, and the shim set grows over time.

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

```sh
sloppy build
sloppy deps .sloppy --explain
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```
