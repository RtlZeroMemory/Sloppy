# Templates

`sloppy create` copies a local starter template. Templates are intentionally
small: they show supported alpha APIs without implying Node or production
framework compatibility.

```sh
sloppy create my-api --template api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
```

## Built-In API Templates

| Template | Use it for |
| --- | --- |
| `minimal-api` | A small route-only API. |
| `api` | A multi-file API layout with route modules, configuration, health endpoints, SQLite metadata, static files, and package flow examples. |
| `package-api` | An API intended to exercise package/dependency artifact output. |

The `api` template is the default backend starter. Useful smoke commands:

```sh
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /public/hello.txt
sloppy run .sloppy --once POST /users --json "{\"name\":\"Ada Lovelace\",\"email\":\"ada@example.test\"}"
sloppy package
sloppy run .sloppy/package --once GET /public/hello.txt
```

## Auth

Auth is available from any API template:

```ts
import { Auth, Config } from "sloppy";

app.use(Auth.jwtBearer({
  issuer: "sloppy.local",
  audience: "api",
  secret: Config.required("Auth:JwtSecret"),
}));
```

Auth setup in templates is public-alpha/experimental. There is no dedicated
auth template yet. Add the auth setup to the generated `app.js` or route
module, then add the corresponding `Auth:*` keys to `appsettings.json` or your
deployment configuration.

## Other Templates

Use `program` for route-free Program Mode, `cli` for command-style Program
Mode, and `node-compat` for explicit partial Node-compatibility experiments.
