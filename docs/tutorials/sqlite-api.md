# Tutorial: Run The SQLite Users API

This tutorial starts from the checked-in example at
`examples/users-api-sqlite`.

## What You Will Run

The example contains:

```text
examples/users-api-sqlite/
  app.js
  sloppy.json
  modules/
    users.js
```

Key routes:

- `GET /health`
- `GET /users`
- `GET /users/{id:int}`
- `POST /users`

## Build With V8 Enabled

From the repository root:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
```

If SDK resolution fails, stop and fix that environment issue first.

## Build And Run The Example

From `examples/users-api-sqlite/`:

```powershell
..\..\build\windows-relwithdebinfo\sloppy.exe build
..\..\build\windows-relwithdebinfo\sloppy.exe run --once GET /users
```

Equivalent CLI shape: `sloppy build` then `sloppy run --once GET /users`.

Expected response body is JSON and contains seeded users. The exact header order
can vary, but the body should contain rows like:

```json
[{"id":1,"name":"Ada Lovelace","email":"ada@example.test"},{"id":2,"name":"Grace Hopper","email":"grace@example.test"}]
```

## Read The Code You Just Ran

`app.js` wires the SQLite provider and module:

```js
import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";
import { usersModule } from "./modules/users.js";

const app = Sloppy.create();

app.use(sqlite("main"));
app.get("/health", () => Results.text("ok"));
app.useModule(usersModule);

export default app;
```

`modules/users.js` resolves `app.provider("sqlite:main")`, seeds rows with
`db.exec(...)`, and returns query results with `Results.json(...)`.

## What Happened

`build` emitted `.sloppy/app.plan.json`, `.sloppy/app.js`, and
`.sloppy/app.js.map` from `sloppy.json` (`entry: app.js`).

`run --once GET /users` loaded those artifacts, executed the users module
handler, and returned rows from the SQLite provider path.

## Evidence Boundaries

Default evidence (non-V8 build):

- Source-input and project build lanes can emit artifacts.
- `run` is expected to fail with `requires V8-enabled build`.

V8-gated evidence:

- Successful `/users` execution and response content are V8-gated runtime
  evidence.
