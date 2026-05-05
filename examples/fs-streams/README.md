# Filesystem Streams Example

Status: CORE-FS-01.J FileHandle/stream example.

`FileHandle` exposes Sloppy-owned async iteration helpers without claiming Node stream
compatibility:

```js
import { Directory, File } from "sloppy/fs";

await Directory.create("./tmp", { recursive: true });
const file = await File.open("data:/large.log", { access: "read" });
try {
    for await (const line of file.readLines()) {
        await File.appendText("./tmp/filtered.log", `${line}\n`);
    }
} finally {
    await file.close();
}
```
