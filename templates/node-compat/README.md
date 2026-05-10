# Node Compatibility Starter

This Program Mode starter demonstrates explicit supported Node compatibility
shims. Sloppy is not full Node. Unsupported builtins fail clearly, and the shim
set grows over time.

This template uses:

- `node:path`
- `node:events`
- `node:buffer`
- `node:querystring`

It intentionally avoids `node:stream`, `node:http`, `node:net`, `node:tls`,
`node:child_process`, native addons, and implicit globals such as `process` or
`Buffer`.

```sh
sloppy build
sloppy deps .sloppy
sloppy run .sloppy
sloppy package
sloppy run .sloppy/package
```
