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

It intentionally avoids `node:http`, `node:net`, `node:tls`,
`node:child_process`, native addons, and implicit globals such as `process` or
`Buffer`. Import compatibility modules explicitly.

```sh
sloppy build
sloppy deps .sloppy
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```
