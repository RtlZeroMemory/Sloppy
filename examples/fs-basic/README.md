# Filesystem Basic Example

Status: CORE-FS-01.J filesystem example. This source example documents the intended
`sloppy/fs` app-facing shape; full source-input execution remains gated by the V8 runtime
lane and current compiler support.

This example uses project-relative paths for local development:

```js
import { Directory, File } from "sloppy/fs";

await Directory.create("./tmp", { recursive: true });
await File.writeJson("./tmp/users.json", [{ id: 1, name: "Ada" }], {
    atomic: true,
    indent: 2,
});
const users = await File.readJson("./tmp/users.json");
```

The example does not claim Node `fs` compatibility or public sync filesystem APIs.
