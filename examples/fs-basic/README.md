# Filesystem Basic Example

This filesystem source example documents the intended
`sloppy/fs` app-facing shape; full source-input execution remains gated by the V8 runtime
lane and current compiler support.

This example uses project-relative paths for local development:

```js
import { Directory, File } from "sloppy/fs";
import { Deadline } from "sloppy/time";

await Directory.create("./tmp", { recursive: true });
await File.writeJson("./tmp/users.json", [{ id: 1, name: "Ada" }], {
    atomic: true,
    indent: 2,
});
const deadline = Deadline.after(1000);
const users = await File.readJson("./tmp/users.json", { deadline });
```

The read uses a Time deadline option. It does not claim preemptive native filesystem
interruption, Node `fs` compatibility, or public sync filesystem APIs.
