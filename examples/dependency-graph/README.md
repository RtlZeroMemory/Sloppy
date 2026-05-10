# Dependency Graph

This example shows a small dependency graph with:

- one local fixture package installed through `file:`;
- one Node compatibility shim (`node:path`);
- one asset included by `assetInclude`;
- a graph that can be inspected with `sloppy deps`.

## Setup

```sh
npm install
sloppy build
sloppy deps .sloppy
sloppy deps .sloppy --format json
```

The package is local to this example and does not require a registry call.
