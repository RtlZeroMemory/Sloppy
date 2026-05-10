# Node Shims: path and events

This example imports two supported Node compatibility shims:

- `node:path`
- `node:events`

It demonstrates explicit compatibility modules. Sloppy does not provide a full
Node runtime or implicit Node globals.

```sh
sloppy build
sloppy deps .sloppy
sloppy run
```
