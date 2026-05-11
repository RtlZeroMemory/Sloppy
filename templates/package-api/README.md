# Package API Starter

This public alpha, pre-production backend starter uses a compatible local
pure-JS package through an npm `file:` dependency. It does not require internet
access.

Package support is experimental. Sloppy consumes packages that are already
installed in `node_modules`, bundles compatible pure-JS modules into the
generated artifacts, and records the graph for `sloppy deps`. This is not full
npm ecosystem compatibility; native addons and N-API are unsupported.
Packages must use JavaScript and runtime APIs Sloppy can transform and shim.
The package resolver supports common `exports`, `imports`, condition,
subpath, JSON, and CommonJS patterns, but unsupported export shapes fail
clearly instead of falling back silently.
The supported Node compatibility surface is documented in
`docs/reference/node-compatibility.md`.

```sh
npm install --ignore-scripts --no-audit
sloppy build
sloppy deps .sloppy
sloppy deps .sloppy --explain
sloppy deps .sloppy --format json
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users/Ada
sloppy package
sloppy run .sloppy/package --once GET /health
sloppy run .sloppy/package --once GET /users/Ada
```

The packaged app contains the bundled dependency graph and does not need
`node_modules` at runtime.
