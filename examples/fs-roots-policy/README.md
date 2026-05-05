# Filesystem Roots And Policy Example

Status: CORE-FS-01.J policy example.

Named roots are the recommended production shape:

```js
import { Directory, File } from "sloppy/fs";

await Directory.create("data:/exports", { recursive: true });
await File.writeText("data:/exports/report.txt", "ready", { atomic: true });
```

Development mode may warn on absolute paths. Strict mode requires explicit policy for
absolute and risky operations such as delete, watch, and lock.
