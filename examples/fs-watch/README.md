# Filesystem Watch Example

Status: CORE-FS-01.J watch example.

The current watch API is resource-backed, bounded, and non-recursive. It reports
create/modify/delete/overflow events without claiming Node `fs.watch` semantics:

```js
import { Directory, File } from "sloppy/fs";

const watcher = await Directory.watch("./tmp", { queueCapacity: 16 });
try {
    for await (const event of watcher) {
        await File.appendText("./tmp/watch-events.log", `${event.kind}:${event.path}\n`);
        break;
    }
} finally {
    await watcher.close();
}
```
