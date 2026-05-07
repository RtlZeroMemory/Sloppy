# Filesystem Roots And Policy Example

Status: filesystem policy example.

Named roots are the recommended Sloppy filesystem policy shape:

```js
import { Directory, File } from "sloppy/fs";

await Directory.create("data:/exports", { recursive: true });
await File.writeText("data:/exports/report.txt", "ready", { atomic: true });
```

Development mode may warn on absolute paths. Strict mode requires explicit policy for
absolute and risky operations such as delete, watch, and lock.
