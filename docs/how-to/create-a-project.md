# How To Create A Project

Create a new Sloppy project that supports `sloppy build` and `sloppy run` from the project root.

## Prerequisites

- `sloppy` and `sloppyc` are installed and on `PATH`.

## Steps

1. Create the project files.

```text
my-api/
  sloppy.json
  src/
    main.ts
```

2. Add `sloppy.json`.

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

3. Add `src/main.ts`.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
app.get("/health", () => Results.text("ok")).withName("Health.Get");

export default app;
```

4. Build from the project root.

```powershell
sloppy build
```

## Expected Result

```text
.sloppy/app.plan.json
.sloppy/app.js
.sloppy/app.js.map
```

## Common Failures

- `missing entry in sloppy.json`: `entry` is not present.
- `invalid sloppy.json: unsupported field`: only `entry`, `outDir`, and `environment` are allowed.
- `invalid sloppy.json: entry must be a relative path inside the project root`: absolute paths and `..` segments are rejected.
