# Dynamic Module Include

This example demonstrates computed dynamic imports over a sealed module graph.

`src/main.ts` imports a plugin by name:

```ts
await import("./plugins/" + name + ".js");
```

The possible plugins are included by `moduleInclude` in `sloppy.json`.

```sh
sloppy build
sloppy deps .sloppy
sloppy run -- alpha
```

Dynamic import controls when a known module is evaluated. It is not
unrestricted runtime file or package discovery.
