# Filesystem Streams Example

This filesystem example shows Sloppy-owned `FileHandle` async iteration helpers.
Node-style stream APIs are separate from this example.

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
